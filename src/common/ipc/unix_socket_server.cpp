//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "unix_socket_server.hpp"

#include "platform/posix_executor_extension.hpp"
#include "platform/posix_utils.hpp"

#include <cetl/cetl.hpp>
#include <cetl/rtti.hpp>
#include <libcyphal/executor.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace
{

constexpr int MaxConnections = 5;

class ClientContextImpl final : public detail::ClientContext
{
public:
    explicit ClientContextImpl(const int client_fd)
        : client_fd_{client_fd}
    {
        CETL_DEBUG_ASSERT(client_fd != -1, "");

        std::cout << "New client connection on fd=" << client_fd << ".\n";
    }

    ~ClientContextImpl() override
    {
        std::cout << "Closing connection on fd=" << client_fd_ << ".\n";

        platform::posixSyscallError([this] {
            //
            return ::close(client_fd_);
        });
    }

    ClientContextImpl(ClientContextImpl&&)                 = delete;
    ClientContextImpl(const ClientContextImpl&)            = delete;
    ClientContextImpl& operator=(ClientContextImpl&&)      = delete;
    ClientContextImpl& operator=(const ClientContextImpl&) = delete;

    int getFd() const
    {
        return client_fd_;
    }

    void setCallback(libcyphal::IExecutor::Callback::Any&& callback)
    {
        callback_ = std::move(callback);
    }

private:
    const int                           client_fd_;
    libcyphal::IExecutor::Callback::Any callback_;

};  // ClientContextImpl

}  // namespace

UnixSocketServer::UnixSocketServer(libcyphal::IExecutor& executor, std::string socket_path)
    : executor_{executor}
    , socket_path_{std::move(socket_path)}
    , server_fd_{-1}
    , posix_executor_ext_{cetl::rtti_cast<platform::IPosixExecutorExtension*>(&executor_)}
{
    CETL_DEBUG_ASSERT(posix_executor_ext_ != nullptr, "");
}

UnixSocketServer::~UnixSocketServer()
{
    if (server_fd_ != -1)
    {
        platform::posixSyscallError([this] {
            //
            return ::close(server_fd_);
        });
    }
}

bool UnixSocketServer::start()
{
    CETL_DEBUG_ASSERT(server_fd_ == -1, "");

    if (const auto err = platform::posixSyscallError([this] {
            //
            return server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        }))
    {
        std::cerr << "Failed to create socket: " << ::strerror(err) << "\n";
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family                        = AF_UNIX;
    const std::string abstract_socket_path = '\0' + socket_path_;
    CETL_DEBUG_ASSERT(abstract_socket_path.size() <= sizeof(addr.sun_path), "");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    std::memcpy(addr.sun_path,
                abstract_socket_path.c_str(),
                std::min(sizeof(addr.sun_path), abstract_socket_path.size()));

    if (const auto err = platform::posixSyscallError([this, &addr, &abstract_socket_path] {
            //
            return ::bind(server_fd_,
                          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                          reinterpret_cast<const sockaddr*>(&addr),
                          offsetof(struct sockaddr_un, sun_path) + abstract_socket_path.size());
        }))
    {
        std::cerr << "Failed to bind socket: " << ::strerror(err) << "\n";
        return false;
    }

    if (const auto err = platform::posixSyscallError([this] {
            //
            return ::listen(server_fd_, MaxConnections);
        }))
    {
        std::cerr << "Failed to listen on socket: " << ::strerror(err) << "\n";
        return false;
    }

    return true;
}

void UnixSocketServer::accept()
{
    CETL_DEBUG_ASSERT(server_fd_ != -1, "");

    int client_fd = -1;
    if (const auto err = platform::posixSyscallError([this, &client_fd] {
            //
            return client_fd = ::accept(server_fd_, nullptr, nullptr);
        }))
    {
        std::cerr << "Failed to accept connection: " << ::strerror(err) << "\n";
        return;
    }

    handle_client_connection(client_fd);
}

void UnixSocketServer::handle_client_connection(const int client_fd)
{
    CETL_DEBUG_ASSERT(client_fd != -1, "");
    CETL_DEBUG_ASSERT(client_contexts_.find(client_fd) == client_contexts_.end(), "");

    auto client_context = std::make_unique<ClientContextImpl>(client_fd);
    client_context->setCallback(posix_executor_ext_->registerAwaitableCallback(
        [this, client_fd](const auto&) {
            //
            handle_client_request(client_fd);
        },
        platform::IPosixExecutorExtension::Trigger::Readable{client_fd}));

    client_contexts_.emplace(client_fd, std::move(client_context));
}

void UnixSocketServer::handle_client_request(const int client_fd)
{
    CETL_DEBUG_ASSERT(client_fd != -1, "");

    constexpr std::size_t      buf_size = 256;
    std::array<char, buf_size> buffer{};
    ssize_t                    bytes_read = 0;
    if (const auto err = platform::posixSyscallError([client_fd, &bytes_read, &buffer] {
            //
            return bytes_read = ::read(client_fd, buffer.data(), buffer.size() - 1);
        }))
    {
        std::cerr << "Failed to read: " << ::strerror(err) << "\n";
        return;
    }
    if (bytes_read == 0)
    {
        // EOF which means the client has closed the connection.
        client_contexts_.erase(client_fd);
        return;
    }

    buffer[bytes_read] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    std::cout << "Received: " << buffer.data() << "\n";

    // Echo back
    //
    if (const auto err = platform::posixSyscallError([client_fd, bytes_read, &buffer] {
            //
            return ::write(client_fd, buffer.data(), bytes_read);
        }))
    {
        std::cerr << "Failed to write: " << ::strerror(err) << "\n";
    }
}

CETL_NODISCARD libcyphal::IExecutor::Callback::Any UnixSocketServer::registerListenCallback(
    libcyphal::IExecutor::Callback::Function&& function) const
{
    CETL_DEBUG_ASSERT(udp_handle_.fd >= 0, "");

    return posix_executor_ext_->registerAwaitableCallback(  //
        std::move(function),
        platform::IPosixExecutorExtension::Trigger::Readable{server_fd_});
}

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd
