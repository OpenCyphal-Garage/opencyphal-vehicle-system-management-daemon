//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_DAEMON_HPP_INCLUDED
#define OCVSMD_SDK_DAEMON_HPP_INCLUDED

#include "file_server.hpp"
#include "node_command_client.hpp"
#include "node_registry_client.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/executor.hpp>

#include <memory>
#include <string>

namespace ocvsmd
{
namespace sdk
{

/// An abstract factory for the specialized interfaces.
///
class Daemon
{
public:
    /// Defines the shared pointer type for the factory.
    ///
    using Ptr = std::shared_ptr<Daemon>;

    /// Creates a new instance of the factory, and establishes a connection to the daemon.
    ///
    /// @param memory The memory resource to use for the factory and its subcomponents.
    ///               The memory resource must outlive the factory.
    ///               In use for IPC (de)serialization only; other functionality uses usual c++ heap.
    /// @param executor The executor to use for the factory and its subcomponents.
    ///                 Instance of the executor must outlive the factory.
    ///                 Should support `IPosixExecutorExtension` interface (via `cetl::rtti`).
    /// @return Shared pointer to the successfully created factory.
    ///         `nullptr` on failure (see logs for the reason of failure).
    ///
    CETL_NODISCARD static Ptr make(cetl::pmr::memory_resource& memory,
                                   libcyphal::IExecutor&       executor,
                                   const std::string&          connection);

    // No copy/move semantics.
    Daemon(Daemon&&)                 = delete;
    Daemon(const Daemon&)            = delete;
    Daemon& operator=(Daemon&&)      = delete;
    Daemon& operator=(const Daemon&) = delete;

    virtual ~Daemon() = default;

    /// Gets a pointer to the shared entity which represents the File Server component of the OCVSMD engine.
    ///
    /// @return Shared pointer to the client side of the File Server component.
    ///         The component is always present in the OCVSMD engine, so the result is never `nullptr`.
    ///
    virtual FileServer::Ptr getFileServer() const = 0;

    /// Gets a pointer to the shared entity which represents the Node Exec Command component of the OCVSMD engine.
    ///
    /// @return Shared pointer to the client side of the Node Exec Command component.
    ///         The component is always present in the OCVSMD engine, so the result is never `nullptr`.
    ///
    virtual NodeCommandClient::Ptr getNodeCommandClient() const = 0;

    /// Gets a pointer to the shared entity which represents the Node Registry component of the OCVSMD engine.
    ///
    /// @return Shared pointer to the client side of the Node Registry component.
    ///         The component is always present in the OCVSMD engine, so the result is never `nullptr`.
    ///
    virtual NodeRegistryClient::Ptr getNodeRegistryClient() const = 0;

protected:
    Daemon() = default;

};  // Daemon

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_DAEMON_HPP_INCLUDED
