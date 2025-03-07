//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "client_router.hpp"

#include "common_helpers.hpp"
#include "dsdl_helpers.hpp"
#include "gateway.hpp"
#include "ipc_types.hpp"
#include "logging.hpp"
#include "pipe/client_pipe.hpp"

#include "ocvsmd/common/ipc/RouteChannelEnd_0_2.hpp"
#include "ocvsmd/common/ipc/RouteChannelMsg_0_1.hpp"
#include "ocvsmd/common/ipc/RouteConnect_0_1.hpp"
#include "ocvsmd/common/ipc/Route_0_2.hpp"
#include "uavcan/primitive/Empty_1_0.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/visit_helpers.hpp>

#include <cerrno>
#include <cstdint>
#include <memory>
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

/// Defines implementation of the IPC client-side router.
///
/// It subscribes to the client pipe events and dispatches them to corresponding target gateway (by matching tags).
/// In case of the pipe disconnection, it is broadcast-ed to all currently existing gateways (aka channels).
///
class ClientRouterImpl final : public ClientRouter
{
public:
    ClientRouterImpl(cetl::pmr::memory_resource& memory, pipe::ClientPipe::Ptr client_pipe)
        : memory_{memory}
        , client_pipe_{std::move(client_pipe)}
        , logger_{getLogger("ipc")}
        , next_tag_{0}
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
        const Endpoint endpoint{next_tag_++};

        auto gateway                   = GatewayImpl::create(*this, endpoint);
        map_of_gateways_[endpoint.tag] = gateway;
        return gateway;
    }

private:
    struct Endpoint final
    {
        using Tag = std::uint64_t;

        const Tag tag;
    };

    /// Defines private IPC gateway entity of this client-side IPC router.
    ///
    /// Gateway is a glue between this IPC router and a service channel.
    /// Lifetime of a gateway is exactly the same as the lifetime of the associated service channel.
    ///
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
            , next_sequence_{0}
            , completion_error_code_{0}
        {
            router_.logger_->trace("Gateway(tag={}).", endpoint.tag);
        }

        GatewayImpl(const GatewayImpl&)                = delete;
        GatewayImpl(GatewayImpl&&) noexcept            = delete;
        GatewayImpl& operator=(const GatewayImpl&)     = delete;
        GatewayImpl& operator=(GatewayImpl&&) noexcept = delete;

        virtual ~GatewayImpl()
        {
            performWithoutThrowing([this] {
                //
                router_.logger_->trace("~Gateway(tag={}, err={}).", endpoint_.tag, completion_error_code_);

                // `next_sequence_ == 0` means that this gateway was never used for sending messages,
                // and so remote router never knew about it (its tag) - no need to post "ChEnd" event.
                router_.onGatewayDisposal(endpoint_, next_sequence_ > 0, completion_error_code_);
            });
        }

        // detail::Gateway

        CETL_NODISCARD int send(const detail::ServiceDesc::Id service_id, const Payload payload) override
        {
            if (!router_.isConnected(endpoint_))
            {
                return static_cast<int>(ErrorCode::NotConnected);
            }
            if (!router_.isRegisteredGateway(endpoint_))
            {
                return static_cast<int>(ErrorCode::Shutdown);
            }

            Route_0_2 route{&router_.memory_};

            auto& channel_msg        = route.set_channel_msg();
            channel_msg.tag          = endpoint_.tag;
            channel_msg.sequence     = next_sequence_++;
            channel_msg.service_id   = service_id;
            channel_msg.payload_size = payload.size();

            return tryPerformOnSerialized(route, [this, payload](const auto prefix) {
                //
                return router_.client_pipe_->send({{prefix, payload}});
            });
        }

        CETL_NODISCARD int complete(const int error_code, const bool keep_alive) override
        {
            if (!router_.isConnected(endpoint_))
            {
                return static_cast<int>(ErrorCode::NotConnected);
            }
            if (!router_.isRegisteredGateway(endpoint_))
            {
                return static_cast<int>(ErrorCode::Shutdown);
            }

            completion_error_code_ = error_code;

            // Zero `next_sequence_` means that this gateway was never used for sending messages,
            // and so remote router never knew about it (its tag) - no need to post "ChEnd" event.
            if (next_sequence_ == 0)
            {
                return 0;
            }

            Route_0_2 route{&router_.memory_};
            auto&     channel_end  = route.set_channel_end();
            channel_end.tag        = endpoint_.tag;
            channel_end.error_code = error_code;
            channel_end.keep_alive = keep_alive;

            return tryPerformOnSerialized(route, [this](const auto payload) {
                //
                return router_.client_pipe_->send({{payload}});
            });
        }

        CETL_NODISCARD int event(const Event::Var& event) override
        {
            // It's fine to be not subscribed to events.
            return (event_handler_) ? event_handler_(event) : 0;
        }

        void subscribe(EventHandler event_handler) override
        {
            event_handler_ = std::move(event_handler);
            router_.onGatewaySubscription(endpoint_);
        }

    private:
        ClientRouterImpl& router_;
        const Endpoint    endpoint_;
        std::uint64_t     next_sequence_;
        EventHandler      event_handler_;
        int               completion_error_code_;

    };  // GatewayImpl

    // Lifetime of a gateway is strictly managed by its channel. But router needs to "weakly" keep track of them.
    using MapOfWeakGateways = std::unordered_map<Endpoint::Tag, detail::Gateway::WeakPtr>;

    CETL_NODISCARD bool isConnected(const Endpoint&) const noexcept
    {
        return is_connected_;
    }

    CETL_NODISCARD bool isRegisteredGateway(const Endpoint& endpoint) const noexcept
    {
        return map_of_gateways_.find(endpoint.tag) != map_of_gateways_.end();
    }

    template <typename Action>
    CETL_NODISCARD int findAndActOnRegisteredGateway(const Endpoint endpoint, Action&& action) const
    {
        const auto tag_to_gw = map_of_gateways_.find(endpoint.tag);
        if (tag_to_gw != map_of_gateways_.end())
        {
            if (const auto gateway = tag_to_gw->second.lock())
            {
                return std::forward<Action>(action)(*gateway, tag_to_gw);
            }
        }

        // It's fine and expected to have no gateway registered for the given endpoint.
        return 0;
    }

    template <typename Action>
    void forEachRegisteredGateway(Action action)
    {
        // Calling an action might indirectly modify the map, so we first
        // collect strong pointers to gateways into a local collection.
        //
        std::vector<detail::Gateway::Ptr> gateway_ptrs;
        gateway_ptrs.reserve(map_of_gateways_.size());
        for (const auto& tag_to_gw : map_of_gateways_)
        {
            if (const auto gateway_ptr = tag_to_gw.second.lock())
            {
                gateway_ptrs.push_back(gateway_ptr);
            }
        }

        for (const auto& gateway_ptr : gateway_ptrs)
        {
            action(*gateway_ptr);
        }
    }

    void onGatewaySubscription(const Endpoint endpoint)
    {
        if (is_connected_)
        {
            const int err = findAndActOnRegisteredGateway(endpoint, [](auto& gateway, auto) {
                //
                return gateway.event(detail::Gateway::Event::Connected{});
            });
            (void) err;  // Best efforts strategy.
        }
    }

    /// Unregisters the gateway associated with the given endpoint.
    ///
    /// Called on the gateway disposal (correspondingly on its channel destruction).
    /// The "dying" gateway might wish to notify the remote router about its disposal.
    /// This local router fulfills the wish if the gateway was registered and the router is connected.
    ///
    void onGatewayDisposal(const Endpoint& endpoint, const bool send_ch_end, const int completion_err)
    {
        const bool was_registered = (map_of_gateways_.erase(endpoint.tag) > 0);

        // Notify "remote" router about the gateway disposal (aka channel completion).
        // The router will propagate "ChEnd" event to the counterpart gateway (if it's registered).
        //
        if (was_registered && send_ch_end && isConnected(endpoint))
        {
            Route_0_2 route{&memory_};
            auto&     channel_end  = route.set_channel_end();
            channel_end.tag        = endpoint.tag;
            channel_end.error_code = completion_err;
            channel_end.keep_alive = false;

            const int error = tryPerformOnSerialized(route, [this](const auto payload) {
                //
                return client_pipe_->send({{payload}});
            });
            // Best efforts strategy - gateway anyway is gone, so nowhere to report.
            (void) error;
        }
    }

    CETL_NODISCARD int handlePipeEvent(const pipe::ClientPipe::Event::Connected) const
    {
        logger_->debug("Pipe is connected.");

        // It's not enough to consider the server route connected by the pipe event.
        // We gonna initiate `RouteConnect` negotiation (see `handleRouteConnect`).
        //
        Route_0_2 route{&memory_};
        auto&     route_conn     = route.set_connect();
        route_conn.version.major = VERSION_MAJOR;
        route_conn.version.minor = VERSION_MINOR;
        //
        return tryPerformOnSerialized(route, [this](const auto payload) {
            //
            return client_pipe_->send({{payload}});
        });
    }

    CETL_NODISCARD int handlePipeEvent(const pipe::ClientPipe::Event::Message& msg)
    {
        Route_0_2  route_msg{&memory_};
        const auto result_size = tryDeserializePayload(msg.payload, route_msg);
        if (!result_size.has_value())
        {
            // Invalid message payload.
            return EINVAL;
        }

        return cetl::visit(         //
            cetl::make_overloaded(  //
                [this](const uavcan::primitive::Empty_1_0&) {
                    //
                    // Unexpected message, but we can't remove it
                    // b/c Nunavut generated code needs a default case.
                    return EINVAL;
                },
                [this](const RouteConnect_0_1& route_conn) {
                    //
                    return handleRouteConnect(route_conn);
                },
                [this, &msg](const RouteChannelMsg_0_1& route_ch_msg) {
                    //
                    return handleRouteChannelMsg(route_ch_msg, msg.payload);
                },
                [this](const RouteChannelEnd_0_2& route_ch_end) {
                    //
                    return handleRouteChannelEnd(route_ch_end);
                }),
            route_msg.union_value);
    }

    CETL_NODISCARD int handlePipeEvent(const pipe::ClientPipe::Event::Disconnected)
    {
        logger_->debug("Pipe is disconnected.");

        if (is_connected_)
        {
            is_connected_ = false;

            // The whole router is disconnected, so we need to unregister and notify all gateways.
            //
            MapOfWeakGateways local_map_of_gateways;
            std::swap(local_map_of_gateways, map_of_gateways_);
            for (const auto& tag_to_gw : local_map_of_gateways)
            {
                if (const auto gateway = tag_to_gw.second.lock())
                {
                    constexpr detail::Gateway::Event::Completed completed{ErrorCode::Disconnected, false};

                    const int err = gateway->event(completed);
                    (void) err;  // Best efforts strategy.
                }
            }
        }

        // It's fine to be already disconnected.
        return 0;
    }

    CETL_NODISCARD int handleRouteConnect(const RouteConnect_0_1& rt_conn)
    {
        logger_->debug("Route connect response (ver='{}.{}', err={}).",
                       static_cast<int>(rt_conn.version.major),
                       static_cast<int>(rt_conn.version.minor),
                       static_cast<int>(rt_conn.error_code));

        if (!is_connected_)
        {
            is_connected_ = true;

            // We've got connection response from the server, so we need to notify all local gateways.
            //
            forEachRegisteredGateway([](auto& gateway) {
                //
                const int err = gateway.event(detail::Gateway::Event::Connected{});
                (void) err;  // Best efforts strategy.
            });
        }

        // It's fine to be already connected.
        return 0;
    }

    CETL_NODISCARD int handleRouteChannelMsg(const RouteChannelMsg_0_1& route_ch_msg, const Payload payload)
    {
        // Cut routing stuff from the payload - remaining is the real message payload.
        const auto msg_real_payload = payload.subspan(payload.size() - route_ch_msg.payload_size);

        const Endpoint endpoint{route_ch_msg.tag};

        const auto tag_to_gw = map_of_gateways_.find(endpoint.tag);
        if (tag_to_gw != map_of_gateways_.end())
        {
            if (const auto gateway = tag_to_gw->second.lock())
            {
                logger_->trace("Route Ch Msg (tag={}, seq={}).", route_ch_msg.tag, route_ch_msg.sequence);

                return gateway->event(detail::Gateway::Event::Message{route_ch_msg.sequence, msg_real_payload});
            }
        }

        // Nothing to do here with unsolicited messages - just trace and ignore them.
        //
        logger_->debug("Route Ch Unsolicited Msg (tag={}, seq={}, srv=0x{:X}).",
                       route_ch_msg.tag,
                       route_ch_msg.sequence,
                       route_ch_msg.service_id);
        return 0;
    }

    CETL_NODISCARD int handleRouteChannelEnd(const RouteChannelEnd_0_2& route_ch_end)
    {
        logger_->debug("Route Ch End (tag={}, err={}, keep_alive={}).",
                       route_ch_end.tag,
                       route_ch_end.error_code,
                       route_ch_end.keep_alive);

        const Endpoint endpoint{route_ch_end.tag};
        const auto     error_code = static_cast<ErrorCode>(route_ch_end.error_code);
        const bool     keep_alive = route_ch_end.keep_alive;

        return findAndActOnRegisteredGateway(endpoint, [this, error_code, keep_alive](auto& gateway, auto found_it) {
            //
            if (!keep_alive)
            {
                map_of_gateways_.erase(found_it);
            }
            const detail::Gateway::Event::Completed completed{error_code, keep_alive};
            return gateway.event(completed);
        });
    }

    cetl::pmr::memory_resource& memory_;
    pipe::ClientPipe::Ptr       client_pipe_;
    LoggerPtr                   logger_;
    Endpoint::Tag               next_tag_;
    bool                        is_connected_;
    MapOfWeakGateways           map_of_gateways_;

};  // ClientRouterImpl

}  // namespace

ClientRouter::Ptr ClientRouter::make(cetl::pmr::memory_resource& memory, pipe::ClientPipe::Ptr client_pipe)
{
    return std::make_shared<ClientRouterImpl>(memory, std::move(client_pipe));
}

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd
