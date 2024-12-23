//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include <ocvsmd/sdk/daemon.hpp>

#include "ipc/unix_socket_client.hpp"

#include <memory>
#include <utility>
#include <unistd.h>

namespace ocvsmd
{
namespace sdk
{
namespace
{

class DaemonImpl final : public Daemon
{
public:
    bool connect()
    {
        return ipc_client_.connect_to_server();
    }

    void send_messages() const override
    {
        ipc_client_.send_message("Hello, world!");
        ::sleep(1);
        ipc_client_.send_message("Goodbye, world!");
        ::sleep(1);
    }

private:
    common::ipc::UnixSocketClient ipc_client_{"/var/run/ocvsmd/local.sock"};

};  // DaemonImpl

}  // namespace

std::unique_ptr<Daemon> Daemon::make()
{
    auto daemon = std::make_unique<DaemonImpl>();
    if (!daemon->connect())
    {
        return nullptr;
    }

    return std::move(daemon);
}

}  // namespace sdk
}  // namespace ocvsmd
