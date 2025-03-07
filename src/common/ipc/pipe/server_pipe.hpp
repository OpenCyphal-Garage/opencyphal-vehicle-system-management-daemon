//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_PIPE_SERVER_PIPE_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_PIPE_SERVER_PIPE_HPP_INCLUDED

#include "io/socket_address.hpp"
#include "ipc/ipc_types.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/executor.hpp>

#include <cstddef>
#include <functional>
#include <memory>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace pipe
{

class ServerPipe
{
public:
    using Ptr = std::unique_ptr<ServerPipe>;

    using ClientId = std::size_t;

    struct Event final
    {
        struct Connected final
        {
            ClientId client_id;
        };
        struct Message final
        {
            ClientId client_id;
            Payload  payload;
        };
        struct Disconnected final
        {
            ClientId client_id;
        };

        using Var = cetl::variant<Connected, Message, Disconnected>;

    };  // Event

    using EventHandler = std::function<int(const Event::Var&)>;

    ServerPipe(const ServerPipe&)                = delete;
    ServerPipe(ServerPipe&&) noexcept            = delete;
    ServerPipe& operator=(const ServerPipe&)     = delete;
    ServerPipe& operator=(ServerPipe&&) noexcept = delete;

    virtual ~ServerPipe() = default;

    CETL_NODISCARD virtual int start(EventHandler event_handler)                       = 0;
    CETL_NODISCARD virtual int send(const ClientId client_id, const Payloads payloads) = 0;

protected:
    ServerPipe() = default;

};  // ServerPipe

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_PIPE_SERVER_PIPE_HPP_INCLUDED
