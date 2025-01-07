//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "server_router.hpp"

#include "dsdl_helpers.hpp"
#include "gateway.hpp"
#include "pipe/pipe_types.hpp"
#include "pipe/server_pipe.hpp"

#include "ocvsmd/common/ipc/RouteChannelMsg_1_0.hpp"
#include "ocvsmd/common/ipc/RouteConnect_1_0.hpp"
#include "ocvsmd/common/ipc/Route_1_0.hpp"
#include "uavcan/primitive/Empty_1_0.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/visit_helpers.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace
{

class ServerRouterImpl final : public ServerRouter
{
public:
    explicit ServerRouterImpl(cetl::pmr::memory_resource& memory, pipe::ServerPipe::Ptr server_pipe)
        : memory_{memory}
        , server_pipe_{std::move(server_pipe)}
    {
        CETL_DEBUG_ASSERT(server_pipe_, "");
    }

    // ServerRouter

    cetl::pmr::memory_resource& memory() override
    {
        return memory_;
    }

    void start() override
    {
        server_pipe_->start([this](const auto& pipe_event_var) {
            //
            return cetl::visit(
                [this](const auto& pipe_event) {
                    //
                    return handlePipeEvent(pipe_event);
                },
                pipe_event_var);
        });
    }

    void registerChannelFactory(const detail::ServiceId  service_id,  //
                                TypeErasedChannelFactory channel_factory) override
    {
        service_id_to_channel_factory_[service_id] = std::move(channel_factory);
    }

private:
    struct Endpoint final
    {
        using Tag      = std::uint64_t;
        using ClientId = pipe::ServerPipe::ClientId;

        Endpoint(const Tag tag, ClientId client_id) noexcept
            : tag_{tag}
            , client_id_{client_id}
        {
        }

        Tag getTag() const noexcept
        {
            return tag_;
        }

        ClientId getClientId() const noexcept
        {
            return client_id_;
        }

        // Hasher

        bool operator==(const Endpoint& other) const noexcept
        {
            return tag_ == other.tag_ && client_id_ == other.client_id_;
        }

        struct Hasher final
        {
            std::size_t operator()(const Endpoint& endpoint) const noexcept
            {
                const std::size_t h1 = std::hash<Tag>{}(endpoint.tag_);
                const std::size_t h2 = std::hash<ClientId>{}(endpoint.client_id_);
                return h1 ^ (h2 << 1ULL);
            }

        };  // Hasher

    private:
        const Tag      tag_;
        const ClientId client_id_;

    };  // Endpoint

    class GatewayImpl final : public std::enable_shared_from_this<GatewayImpl>, public detail::Gateway
    {
        struct Private
        {
            explicit Private() = default;
        };

    public:
        static std::shared_ptr<GatewayImpl> create(ServerRouterImpl& router, const Endpoint& endpoint)
        {
            return std::make_shared<GatewayImpl>(Private(), router, endpoint);
        }

        GatewayImpl(Private, ServerRouterImpl& router, const Endpoint& endpoint)
            : router_{router}
            , endpoint_{endpoint}
        {
        }

        GatewayImpl(const GatewayImpl&)                = delete;
        GatewayImpl(GatewayImpl&&) noexcept            = delete;
        GatewayImpl& operator=(const GatewayImpl&)     = delete;
        GatewayImpl& operator=(GatewayImpl&&) noexcept = delete;

        ~GatewayImpl()
        {
            router_.unregisterGateway(endpoint_);
        }

        void send(const detail::ServiceId service_id, const pipe::Payload payload) override
        {
            Route_1_0 route{&router_.memory_};
            auto&     channel_msg  = route.set_channel_msg();
            channel_msg.tag        = endpoint_.getTag();
            channel_msg.service_id = service_id;

            tryPerformOnSerialized(route, [this, payload](const auto prefix) {
                //
                std::array<pipe::Payload, 2> fragments{prefix, payload};
                return router_.server_pipe_->sendMessage(endpoint_.getClientId(), fragments);
            });
        }

        void event(const Event::Var& event) override
        {
            if (event_handler_)
            {
                event_handler_(event);
            }
        }

        void setEventHandler(EventHandler event_handler) override
        {
            router_.registerGateway(endpoint_, shared_from_this());
            event_handler_ = std::move(event_handler);
        }

    private:
        ServerRouterImpl& router_;
        const Endpoint    endpoint_;
        EventHandler      event_handler_;

    };  // GatewayImpl

    using ServiceIdToChannelFactory = std::unordered_map<detail::ServiceId, TypeErasedChannelFactory>;
    using EndpointToWeakGateway     = std::unordered_map<Endpoint, detail::Gateway::WeakPtr, Endpoint::Hasher>;

    void registerGateway(const Endpoint& endpoint, detail::Gateway::WeakPtr gateway)
    {
        endpoint_to_gateway_[endpoint] = std::move(gateway);
    }

    void unregisterGateway(const Endpoint& endpoint)
    {
        endpoint_to_gateway_.erase(endpoint);
    }

    static int handlePipeEvent(const pipe::ServerPipe::Event::Connected)
    {
        // TODO: Implement!
        return 0;
    }

    int handlePipeEvent(const pipe::ServerPipe::Event::Message& msg)
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
                        [this, &msg](const RouteConnect_1_0& route_conn) {
                            //
                            handleRouteConnect(msg.client_id, route_conn);
                        },
                        [this, &msg, msg_payload](const RouteChannelMsg_1_0& route_channel) {
                            //
                            handleRouteChannelMsg(msg.client_id, route_channel, msg_payload);
                        }),
                    route_msg.union_value);

        return 0;

        return 0;
    }

    static int handlePipeEvent(const pipe::ServerPipe::Event::Disconnected)
    {
        // TODO: Implement! disconnected for all gateways which belong to the corresponding client id
        return 0;
    }

    void handleRouteConnect(const pipe::ServerPipe::ClientId client_id, const RouteConnect_1_0&) const
    {
        // TODO: log client route connection

        Route_1_0 route{&memory_};
        auto&     route_conn     = route.set_connect();
        route_conn.version.major = VERSION_MAJOR;
        route_conn.version.minor = VERSION_MINOR;

        tryPerformOnSerialized<Route_1_0>(route, [this, client_id](const auto payload) {
            //
            std::array<pipe::Payload, 1> payloads{payload};
            return server_pipe_->sendMessage(client_id, payloads);
        });
    }

    void handleRouteChannelMsg(const pipe::ServerPipe::ClientId client_id,
                               const RouteChannelMsg_1_0&       route_channel_msg,
                               pipe::Payload                    msg_payload)
    {
        const Endpoint endpoint{route_channel_msg.tag, client_id};

        const auto ep_to_gw = endpoint_to_gateway_.find(endpoint);
        if (ep_to_gw != endpoint_to_gateway_.end())
        {
            auto gateway = ep_to_gw->second.lock();
            if (gateway)
            {
                gateway->event(detail::Gateway::Event::Message{msg_payload});
            }
            return;
        }

        const auto si_to_cf = service_id_to_channel_factory_.find(route_channel_msg.service_id);
        if (si_to_cf != service_id_to_channel_factory_.end())
        {
            auto gateway = GatewayImpl::create(*this, endpoint);

            endpoint_to_gateway_[endpoint] = gateway;
            si_to_cf->second(gateway, msg_payload);
        }

        // TODO: log unsolicited message
    }

    cetl::pmr::memory_resource& memory_;
    pipe::ServerPipe::Ptr       server_pipe_;
    EndpointToWeakGateway       endpoint_to_gateway_;
    ServiceIdToChannelFactory   service_id_to_channel_factory_;

};  // ClientRouterImpl

}  // namespace

ServerRouter::Ptr ServerRouter::make(cetl::pmr::memory_resource& memory, pipe::ServerPipe::Ptr server_pipe)
{
    return std::make_unique<ServerRouterImpl>(memory, std::move(server_pipe));
}

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd