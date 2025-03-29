//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_DAEMON_HPP_INCLUDED
#define OCVSMD_SDK_DAEMON_HPP_INCLUDED

#include "defines.hpp"
#include "execution.hpp"
#include "file_server.hpp"
#include "node_command_client.hpp"
#include "node_pub_sub.hpp"
#include "node_registry_client.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/executor.hpp>

#include <cstddef>
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
    /// @param connection The IPC connection string to the daemon.
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

    /// Defines the result type of the raw publisher creation.
    ///
    /// On success, the result is a smart pointer to a raw publisher with the required parameters.
    /// On failure, the result is an SDK error.
    ///
    struct MakeRawPublisher final
    {
        using Success = RawPublisher::Ptr;
        using Failure = Error;
        using Result  = cetl::variant<Success, Failure>;
    };
    /// Makes a new raw publisher for the specified subject.
    ///
    /// The server-side (the daemon) of SDK will create the corresponding Cyphal network publisher,
    /// and then publish raw messages which are passed from to the client-side of SDK.
    /// See also `RawPublisher` docs for how to publish the outgoing messages.
    ///
    /// @param subject_id The subject ID to publish to.
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<MakeRawPublisher::Result>::Ptr makeRawPublisher(const CyphalPortId subject_id) = 0;

    /// Defines the result type of the raw subscriber creation.
    ///
    /// On success, the result is a smart pointer to a raw subscriber with the required parameters.
    /// On failure, the result is an SDK error.
    ///
    struct MakeRawSubscriber final
    {
        using Success = RawSubscriber::Ptr;
        using Failure = Error;
        using Result  = cetl::variant<Success, Failure>;
    };
    /// Makes a new raw subscriber for the specified subject.
    ///
    /// The server-side (the daemon) of SDK will create the corresponding Cyphal network subscriber,
    /// subscribe to its raw (aka `void`) messages, and then forward them to the client-side of SDK.
    /// See also `RawSubscriber` docs for how to consume the incoming messages.
    ///
    /// @param subject_id The subject ID to subscribe to.
    /// @param extent_bytes The "extent" size of raw messages (see Cyphal spec).
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<MakeRawSubscriber::Result>::Ptr makeRawSubscriber(const CyphalPortId subject_id,
                                                                       const std::size_t  extent_bytes) = 0;

protected:
    Daemon() = default;

};  // Daemon

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_DAEMON_HPP_INCLUDED
