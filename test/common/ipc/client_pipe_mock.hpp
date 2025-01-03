//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_CLIENT_PIPE_MOCK_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_CLIENT_PIPE_MOCK_HPP_INCLUDED

#include "ipc/client_pipe.hpp"
#include "unique_ptr_refwrapper.hpp"

#include <gmock/gmock.h>

#include <functional>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

class ClientPipeMock : public ClientPipe
{
public:
    struct RefWrapper final : UniquePtrRefWrapper<ClientPipe, ClientPipeMock>
    {
        using UniquePtrRefWrapper::UniquePtrRefWrapper;

        // MARK: ClientPipe

        int start(EventHandler event_handler) override
        {
            return reference().start(event_handler);
        }
        int sendMessage(const Payload payload) override
        {
            return reference().sendMessage(payload);
        }

    };  // RefWrapper

    MOCK_METHOD(void, deinit, (), (const));
    MOCK_METHOD(int, start, (EventHandler event_handler), (override));
    MOCK_METHOD(int, sendMessage, (const Payload payload), (override));

};  // ClientPipeMock

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_CLIENT_PIPE_MOCK_HPP_INCLUDED
