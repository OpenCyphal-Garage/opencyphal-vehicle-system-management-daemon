//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_SERVER_ROUTER_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_SERVER_ROUTER_HPP_INCLUDED

#include "channel.hpp"
#include "gateway.hpp"
#include "pipe/pipe_types.hpp"
#include "pipe/server_pipe.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <functional>
#include <memory>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

class ServerRouter
{
public:
    using Ptr = std::unique_ptr<ServerRouter>;

    static Ptr make(cetl::pmr::memory_resource& memory, pipe::ServerPipe::Ptr server_pipe);

    ServerRouter(const ServerRouter&)                = delete;
    ServerRouter(ServerRouter&&) noexcept            = delete;
    ServerRouter& operator=(const ServerRouter&)     = delete;
    ServerRouter& operator=(ServerRouter&&) noexcept = delete;

    virtual ~ServerRouter() = default;

    virtual void                        start()  = 0;
    virtual cetl::pmr::memory_resource& memory() = 0;

    template <typename Input, typename Output>
    using NewChannelHandler = std::function<void(Channel<Input, Output>&& new_channel, const Input& input)>;

    template <typename Input, typename Output>
    void registerChannel(const cetl::string_view service_name, NewChannelHandler<Input, Output> handler)
    {
        CETL_DEBUG_ASSERT(handler, "");

        const auto service_id = AnyChannel::getServiceId<Input>(service_name);

        registerChannelFactory(  //
            service_id,
            [this, service_id, new_ch_handler = std::move(handler)](detail::Gateway::Ptr gateway,
                                                                    const pipe::Payload  payload) {
                Input input{&memory()};
                if (tryDeserializePayload(payload, input))
                {
                    new_ch_handler(Channel<Input, Output>{memory(), gateway, service_id}, input);
                }
            });
    }

protected:
    using TypeErasedChannelFactory = std::function<void(detail::Gateway::Ptr gateway, const pipe::Payload payload)>;

    ServerRouter() = default;

    virtual void registerChannelFactory(const detail::ServiceId  service_id,
                                        TypeErasedChannelFactory channel_factory) = 0;

};  // ServerRouter

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_SERVER_ROUTER_HPP_INCLUDED