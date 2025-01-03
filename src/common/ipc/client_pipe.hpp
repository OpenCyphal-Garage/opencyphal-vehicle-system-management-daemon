//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_CLIENT_PIPE_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_CLIENT_PIPE_HPP_INCLUDED

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

class ClientPipe
{
public:
    using Ptr = std::unique_ptr<ClientPipe>;

    using Payload = cetl::span<const std::uint8_t>;

    struct Event
    {
        struct Connected
        {};
        struct Disconnected
        {};
        struct Message
        {
            Payload payload;

        };  // Message

        using Var = cetl::variant<Message, Connected, Disconnected>;

    };  // Event

    using EventHandler = std::function<int(const Event::Var&)>;

    ClientPipe(ClientPipe&&)                 = delete;
    ClientPipe(const ClientPipe&)            = delete;
    ClientPipe& operator=(ClientPipe&&)      = delete;
    ClientPipe& operator=(const ClientPipe&) = delete;

    virtual ~ClientPipe() = default;

    virtual int start(EventHandler event_handler)  = 0;
    virtual int sendMessage(const Payload payload) = 0;

protected:
    ClientPipe() = default;

};  // ClientPipe

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_CLIENT_PIPE_HPP_INCLUDED
