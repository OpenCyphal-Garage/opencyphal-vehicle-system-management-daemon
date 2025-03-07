//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_PIPE_CLIENT_PIPE_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_PIPE_CLIENT_PIPE_HPP_INCLUDED

#include "ipc/ipc_types.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

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

class ClientPipe
{
public:
    using Ptr = std::unique_ptr<ClientPipe>;

    struct Event final
    {
        struct Connected final
        {};
        struct Message final
        {
            Payload payload;
        };
        struct Disconnected final
        {};

        using Var = cetl::variant<Connected, Message, Disconnected>;

    };  // Event

    using EventHandler = std::function<int(const Event::Var&)>;

    ClientPipe(const ClientPipe&)                = delete;
    ClientPipe(ClientPipe&&) noexcept            = delete;
    ClientPipe& operator=(const ClientPipe&)     = delete;
    ClientPipe& operator=(ClientPipe&&) noexcept = delete;

    virtual ~ClientPipe() = default;

    CETL_NODISCARD virtual int start(EventHandler event_handler) = 0;
    CETL_NODISCARD virtual int send(const Payloads payloads)     = 0;

protected:
    ClientPipe() = default;

};  // ClientPipe

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_PIPE_CLIENT_PIPE_HPP_INCLUDED
