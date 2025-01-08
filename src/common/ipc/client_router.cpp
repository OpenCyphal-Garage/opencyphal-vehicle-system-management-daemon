//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "client_router.hpp"

#include "dsdl_helpers.hpp"
#include "gateway.hpp"
#include "pipe/client_pipe.hpp"
#include "pipe/pipe_types.hpp"

#include "ocvsmd/common/ipc/RouteChannelEnd_1_0.hpp"
#include "ocvsmd/common/ipc/RouteChannelMsg_1_0.hpp"
#include "ocvsmd/common/ipc/RouteConnect_1_0.hpp"
#include "ocvsmd/common/ipc/Route_1_0.hpp"
#include "uavcan/primitive/Empty_1_0.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/visit_helpers.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sys/syslog.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace
{

class ClientRouterImpl final : public ClientRouter
{
public:
    ClientRouterImpl(cetl::pmr::memory_resource& memory, pipe::ClientPipe::Ptr client_pipe)
        : memory_{memory}
        , client_pipe_{std::move(client_pipe)}
        , last_unique_tag_{0}
        , is_connected_{false}
    {
        CETL_DEBUG_ASSERT(client_pipe_, "");
    }

    // ClientRouter

    CETL_NODISCARD cetl::pmr::memory_resource& memory() override
    {
        return memory_;
    }

    CETL_NODISCARD int start() override
    {
        return client_pipe_->start([this](const auto& pipe_event_var) {
            //
            return cetl::visit(
                [this](const auto& pipe_event) {
                    //
                    return handlePipeEvent(pipe_event);
                },
                pipe_event_var);
        });
    }

    CETL_NODISCARD detail::Gateway::Ptr makeGateway() override
    {
        const Endpoint endpoint{++last_unique_tag_};
        return GatewayImpl::create(*this, endpoint);
    }

private:
    struct Endpoint final
    {
        using Tag = std::uint64_t;

        explicit Endpoint(const Tag tag) noexcept
            : tag_{tag}
        {
        }

        CETL_NODISCARD Tag getTag() const noexcept
        {
            return tag_;
        }

        // Hasher

        CETL_NODISCARD bool operator==(const Endpoint& other) const noexcept
        {
            return tag_ == other.tag_;
        }

        struct Hasher final
        {
            CETL_NODISCARD std::size_t operator()(const Endpoint& endpoint) const noexcept
            {
                return std::hash<Tag>{}(endpoint.tag_);
            }

        };  // Hasher

    private:
        const Tag tag_;

    };  // Endpoint

    class GatewayImpl final : public std::enable_shared_from_this<GatewayImpl>, public detail::Gateway
    {
        struct Private
        {
            explicit Private() = default;
        };

    public:
        CETL_NODISCARD static std::shared_ptr<GatewayImpl> create(ClientRouterImpl& router, const Endpoint& endpoint)
        {
            return std::make_shared<GatewayImpl>(Private(), router, endpoint);
        }

        GatewayImpl(Private, ClientRouterImpl& router, const Endpoint& endpoint)
            : router_{router}
            , endpoint_{endpoint}
            , sequence_{0}
        {
            ::syslog(LOG_DEBUG, "Gateway(tag=%zu).", endpoint.getTag());
        }

        GatewayImpl(const GatewayImpl&)                = delete;
        GatewayImpl(GatewayImpl&&) noexcept            = delete;
        GatewayImpl& operator=(const GatewayImpl&)     = delete;
        GatewayImpl& operator=(GatewayImpl&&) noexcept = delete;

        ~GatewayImpl()
        {
            router_.unregisterGateway(endpoint_, true);
            ::syslog(LOG_DEBUG, "~Gateway(tag=%zu).", endpoint_.getTag());
        }

        // detail::Gateway

        CETL_NODISCARD int send(const detail::ServiceId service_id, const pipe::Payload payload) override
        {
            if (!router_.is_connected_)
            {
                return ENOTCONN;
            }

            Route_1_0 route{&router_.memory_};

            auto& channel_msg      = route.set_channel_msg();
            channel_msg.service_id = service_id;
            channel_msg.tag        = endpoint_.getTag();
            channel_msg.sequence   = sequence_++;

            return tryPerformOnSerialized(route, [this, payload](const auto prefix) {
                //
                return router_.client_pipe_->send({{prefix, payload}});
            });
        }

        void event(const Event::Var& event) override
        {
            if (event_handler_)
            {
                event_handler_(event);
            }
        }

        void subscribe(EventHandler event_handler) override
        {
            if (event_handler)
            {
                event_handler_ = std::move(event_handler);
                router_.registerGateway(endpoint_, *this);
            }
            else
            {
                event_handler_ = nullptr;
                router_.unregisterGateway(endpoint_);
            }
        }

    private:
        ClientRouterImpl& router_;
        const Endpoint    endpoint_;
        std::uint64_t     sequence_;
        EventHandler      event_handler_;

    };  // GatewayImpl

    using EndpointToWeakGateway = std::unordered_map<Endpoint, detail::Gateway::WeakPtr, Endpoint::Hasher>;

    CETL_NODISCARD bool isConnected(const Endpoint&) const noexcept
    {
        return is_connected_;
    }

    void registerGateway(const Endpoint& endpoint, GatewayImpl& gateway)
    {
        endpoint_to_gateway_[endpoint] = gateway.shared_from_this();
        if (is_connected_)
        {
            gateway.event(detail::Gateway::Event::Connected{});
        }
    }

    void unregisterGateway(const Endpoint& endpoint, const bool is_disposed = false)
    {
        endpoint_to_gateway_.erase(endpoint);

        // Notify "remote" router about the gateway disposal.
        // The router will deliver "disconnected" event to the counterpart gateway (if it exists).
        //
        if (is_disposed && isConnected(endpoint))
        {
            Route_1_0 route{&memory_};
            auto&     channel_end  = route.set_channel_end();
            channel_end.tag        = endpoint.getTag();
            channel_end.error_code = 0;

            const int result = tryPerformOnSerialized(route, [this](const auto payload) {
                //
                return client_pipe_->send({{payload}});
            });
            (void) result;
        }
    }

    template <typename Action>
    void forEachGateway(Action action) const
    {
        // Calling an action might indirectly modify the map, so we first
        // collect strong pointers to gateways into a local collection.
        //
        std::vector<detail::Gateway::Ptr> gateways;
        gateways.reserve(endpoint_to_gateway_.size());
        for (const auto& ep_to_gw : endpoint_to_gateway_)
        {
            const auto gateway = ep_to_gw.second.lock();
            if (gateway)
            {
                gateways.push_back(gateway);
            }
        }

        for (const auto& gateway : gateways)
        {
            action(gateway);
        }
    }

    CETL_NODISCARD int handlePipeEvent(const pipe::ClientPipe::Event::Connected) const
    {
        // TODO: log client pipe connection

        Route_1_0 route{&memory_};
        auto&     route_conn     = route.set_connect();
        route_conn.version.major = VERSION_MAJOR;
        route_conn.version.minor = VERSION_MINOR;

        return tryPerformOnSerialized(route, [this](const auto payload) {
            //
            return client_pipe_->send({{payload}});
        });
    }

    CETL_NODISCARD int handlePipeEvent(const pipe::ClientPipe::Event::Message& msg)
    {
        Route_1_0  route_msg{&memory_};
        const auto result_size = tryDeserializePayload(msg.payload, route_msg);
        if (!result_size.has_value())
        {
            return EINVAL;
        }

        // Cut routing stuff from the payload - remaining is the actual message payload.
        const auto msg_payload = msg.payload.subspan(result_size.value());

        cetl::visit(cetl::make_overloaded(
                        //
                        [this](const uavcan::primitive::Empty_1_0&) {},
                        [this](const RouteConnect_1_0& route_conn) {
                            //
                            handleRouteConnect(route_conn);
                        },
                        [this, msg_payload](const RouteChannelMsg_1_0& route_ch_msg) {
                            //
                            handleRouteChannelMsg(route_ch_msg, msg_payload);
                        },
                        [this, msg_payload](const RouteChannelEnd_1_0& route_ch_end) {
                            //
                            handleRouteChannelEnd(route_ch_end);
                        }),
                    route_msg.union_value);

        return 0;
    }

    CETL_NODISCARD int handlePipeEvent(const pipe::ClientPipe::Event::Disconnected)
    {
        // TODO: log client pipe disconnection

        if (is_connected_)
        {
            is_connected_ = false;

            // The whole router is disconnected, so we need to notify all local gateways.
            //
            forEachGateway([](const auto& gateway) {
                //
                gateway->event(detail::Gateway::Event::Disconnected{});
            });
        }
        return 0;
    }

    void handleRouteConnect(const RouteConnect_1_0&)
    {
        // TODO: log server route connection

        if (!is_connected_)
        {
            is_connected_ = true;

            // We've got connection response from the server, so we need to notify all local gateways.
            //
            forEachGateway([](const auto& gateway) {
                //
                gateway->event(detail::Gateway::Event::Connected{});
            });
        }
    }

    void handleRouteChannelMsg(const RouteChannelMsg_1_0& route_ch_msg, pipe::Payload payload)
    {
        const Endpoint endpoint{route_ch_msg.tag};

        const auto ep_to_gw = endpoint_to_gateway_.find(endpoint);
        if (ep_to_gw != endpoint_to_gateway_.end())
        {
            const auto gateway = ep_to_gw->second.lock();
            if (gateway)
            {
                gateway->event(detail::Gateway::Event::Message{route_ch_msg.sequence, payload});
                return;
            }
        }

        // TODO: log unsolicited message
    }

    void handleRouteChannelEnd(const RouteChannelEnd_1_0& route_ch_end) {}

    cetl::pmr::memory_resource& memory_;
    pipe::ClientPipe::Ptr       client_pipe_;
    Endpoint::Tag               last_unique_tag_;
    EndpointToWeakGateway       endpoint_to_gateway_;
    bool                        is_connected_;

};  // ClientRouterImpl

}  // namespace

CETL_NODISCARD ClientRouter::Ptr ClientRouter::make(cetl::pmr::memory_resource& memory,
                                                    pipe::ClientPipe::Ptr       client_pipe)
{
    return std::make_unique<ClientRouterImpl>(memory, std::move(client_pipe));
}

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd
