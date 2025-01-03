//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_CLIENT_ROUTER_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_CLIENT_ROUTER_HPP_INCLUDED

#include <memory>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

class ClientRouter
{
public:
    static std::unique_ptr<ClientRouter> make();

    ClientRouter(ClientRouter&&)                 = delete;
    ClientRouter(const ClientRouter&)            = delete;
    ClientRouter& operator=(ClientRouter&&)      = delete;
    ClientRouter& operator=(const ClientRouter&) = delete;

    virtual ~ClientRouter() = default;

protected:
    ClientRouter() = default;

};  // ClientRouter

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_CLIENT_ROUTER_HPP_INCLUDED