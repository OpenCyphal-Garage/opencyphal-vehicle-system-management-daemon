//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_UNIX_SOCKET_SERVER_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_UNIX_SOCKET_SERVER_HPP_INCLUDED

#include "platform/posix_executor_extension.hpp"

#include <cetl/cetl.hpp>
#include <libcyphal/executor.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace detail
{

class ClientContext
{
public:
    ClientContext() = default;

    ClientContext(ClientContext&&)                 = delete;
    ClientContext(const ClientContext&)            = delete;
    ClientContext& operator=(ClientContext&&)      = delete;
    ClientContext& operator=(const ClientContext&) = delete;

    virtual ~ClientContext() = default;

};  // ClientContext

}  // namespace detail

class UnixSocketServer final
{
public:
    UnixSocketServer(libcyphal::IExecutor& executor, std::string socket_path);

    UnixSocketServer(UnixSocketServer&&)                 = delete;
    UnixSocketServer(const UnixSocketServer&)            = delete;
    UnixSocketServer& operator=(UnixSocketServer&&)      = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;

    ~UnixSocketServer();

    bool start();

    CETL_NODISCARD libcyphal::IExecutor::Callback::Any registerListenCallback(
        libcyphal::IExecutor::Callback::Function&& function) const;

    void accept();

private:
    void handle_client_connection(const int client_fd);
    void handle_client_request(const int client_fd);

    libcyphal::IExecutor&                                           executor_;
    const std::string                                               socket_path_;
    int                                                             server_fd_;
    platform::IPosixExecutorExtension* const                        posix_executor_ext_;
    std::unordered_map<int, std::unique_ptr<detail::ClientContext>> client_contexts_;

};  // UnixSocketServer

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_UNIX_SOCKET_SERVER_HPP_INCLUDED
