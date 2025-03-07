//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_NODE_REGISTRY_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_NODE_REGISTRY_CLIENT_HPP_INCLUDED

#include "execution.hpp"

#include <uavcan/_register/Value_1_0.hpp>

#include <cetl/pf20/cetlpf.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ocvsmd
{
namespace sdk
{

/// Defines client side interface of the OCVSMD Node Registry component.
///
class NodeRegistryClient
{
public:
    /// Defines the shared pointer type for the interface.
    ///
    using Ptr = std::shared_ptr<NodeRegistryClient>;

    NodeRegistryClient(NodeRegistryClient&&)                 = delete;
    NodeRegistryClient(const NodeRegistryClient&)            = delete;
    NodeRegistryClient& operator=(NodeRegistryClient&&)      = delete;
    NodeRegistryClient& operator=(const NodeRegistryClient&) = delete;

    virtual ~NodeRegistryClient() = default;

    /// Defines the result type of the list command execution.
    ///
    /// On success, the result is a map of node ID to its register names (or error code from the node).
    /// Missing Cyphal nodes (or failed to respond in a given timeout) are included in the map with ETIMEDOUT code.
    /// On failure, the result is an error code of some failure to communicate with the OCVSMD engine.
    ///
    struct List final
    {
        /// Defines the result type of the list of a node registers.
        ///
        /// On success, the result is a vector of register names.
        /// On failure, the result is an error code.
        ///
        struct NodeRegisters final
        {
            using Success = std::vector<std::string>;
            using Failure = int;  // `errno`-like error code.
            using Result  = cetl::variant<Success, Failure>;

            NodeRegisters() = delete;
        };

        using Success = std::unordered_map<std::uint16_t, NodeRegisters::Result>;
        using Failure = int;  // `errno`-like error code.
        using Result  = cetl::variant<Success, Failure>;

        List() = delete;
    };
    /// Collects list of register names from the specified Cyphal network nodes.
    ///
    /// On the OCVSMD engine side, Cyphal `385.List.1.0` requests are sent concurrently to all specified Cyphal nodes.
    /// Responses are sent back to the client side as they arrive, and collected in the result map.
    /// The overall result will be available when the last response has arrived, or the timeout has expired.
    ///
    /// @param node_ids The set of Cyphal node IDs to be `list`-ed. Duplicates are ignored.
    /// @param timeout The maximum time to wait for all Cyphal node responses to arrive.
    /// @return An execution sender which emits the async overall result of the operation.
    ///
    virtual SenderOf<List::Result>::Ptr list(const cetl::span<const std::uint16_t> node_ids,
                                             const std::chrono::microseconds       timeout) = 0;

    struct Access final
    {
        using RegValue = uavcan::_register::Value_1_0;

        struct RegNameValue final
        {
            std::string                  name;
            cetl::variant<RegValue, int> value;  // `errno`-like error code.
        };

        struct NodeRegisters final
        {
            using Success = std::vector<RegNameValue>;
            using Failure = int;  // `errno`-like error code.
            using Result  = cetl::variant<Success, Failure>;

            NodeRegisters() = delete;
        };

        using Success = std::unordered_map<std::uint16_t, NodeRegisters::Result>;
        using Failure = int;  // `errno`-like error code.
        using Result  = cetl::variant<Success, Failure>;

        Access() = delete;
    };
    virtual SenderOf<Access::Result>::Ptr read(const cetl::span<const std::uint16_t>     node_ids,
                                               const cetl::span<const cetl::string_view> registers,
                                               const std::chrono::microseconds           timeout) = 0;

protected:
    NodeRegistryClient() = default;

};  // NodeRegistryClient

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_NODE_REGISTRY_CLIENT_HPP_INCLUDED
