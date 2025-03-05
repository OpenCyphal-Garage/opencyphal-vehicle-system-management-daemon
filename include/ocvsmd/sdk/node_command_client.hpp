//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_NODE_COMMAND_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_NODE_COMMAND_CLIENT_HPP_INCLUDED

#include "execution.hpp"

#include <uavcan/node/ExecuteCommand_1_3.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace ocvsmd
{
namespace sdk
{

/// Defines client side interface of the OCVSMD Node Exec Command component.
///
class NodeCommandClient
{
public:
    /// Defines the shared pointer type for the interface.
    ///
    using Ptr = std::shared_ptr<NodeCommandClient>;

    NodeCommandClient(NodeCommandClient&&)                 = delete;
    NodeCommandClient(const NodeCommandClient&)            = delete;
    NodeCommandClient& operator=(NodeCommandClient&&)      = delete;
    NodeCommandClient& operator=(const NodeCommandClient&) = delete;

    virtual ~NodeCommandClient() = default;

    /// Defines the result type of the command execution.
    ///
    /// On success, the result is a map of node IDs to their responses (`status` and `output` params).
    /// Missing Cyphal nodes (or failed to respond in a given timeout) are not included in the map.
    ///
    struct Command final
    {
        using NodeRequest  = uavcan::node::ExecuteCommand_1_3::Request;
        using NodeResponse = uavcan::node::ExecuteCommand_1_3::Response;

        using Success = std::unordered_map<std::uint16_t, NodeResponse>;
        using Failure = int;  // `errno`-like error code.
        using Result  = cetl::variant<Success, Failure>;

        Command() = delete;

    };  // Command

    /// Sends a Cyphal command to the specified Cyphal network nodes.
    ///
    /// On the OCVSMD engine side, the `node_request` is sent concurrently to all specified Cyphal nodes.
    /// Responses are sent back to the client side as they arrive.
    /// Result will be available when the last response has arrived, or the timeout has expired.
    ///
    /// @param node_ids The list of Cyphal node IDs to send the command to. Duplicates are ignored.
    /// @param node_request The Cyphal command request to send (aka broadcast) to the `node_ids`.
    /// @param timeout The maximum time to wait for all Cyphal node responses to arrive.
    /// @return An execution sender which emits the async overall result of the operation.
    ///
    virtual SenderOf<Command::Result>::Ptr sendCommand(const cetl::span<const std::uint16_t> node_ids,
                                                       const Command::NodeRequest&           node_request,
                                                       const std::chrono::microseconds       timeout) = 0;

    /// A convenience method for invoking `sendCommand` with COMMAND_RESTART.
    ///
    /// @param node_ids The list of Cyphal node IDs to send the command to. Duplicates are ignored.
    /// @param timeout The maximum time to wait for all Cyphal node responses to arrive. Default is 1 second.
    /// @return An execution sender which emits the async result of the operation.
    ///
    SenderOf<Command::Result>::Ptr restart(  //
        const cetl::span<const std::uint16_t> node_ids,
        const std::chrono::microseconds       timeout = std::chrono::seconds{1});

    /// A convenience method for invoking `sendCommand` with COMMAND_BEGIN_SOFTWARE_UPDATE.
    ///
    /// @param node_ids The list of Cyphal node IDs to send the command to. Duplicates are ignored.
    /// @param file_path The path to the software update file. Limited to 255 characters.
    ///                  Relative to one of the roots configured in the file server.
    /// @param timeout The maximum time to wait for all Cyphal node responses to arrive. Default is 1 second.
    /// @return An execution sender which emits the async result of the operation.
    ///
    SenderOf<Command::Result>::Ptr beginSoftwareUpdate(  //
        const cetl::span<const std::uint16_t> node_ids,
        const cetl::string_view               file_path,
        const std::chrono::microseconds       timeout = std::chrono::seconds{1});

protected:
    NodeCommandClient() = default;

    virtual cetl::pmr::memory_resource& getMemoryResource() const noexcept = 0;

};  // NodeCommandClient

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_NODE_COMMAND_CLIENT_HPP_INCLUDED
