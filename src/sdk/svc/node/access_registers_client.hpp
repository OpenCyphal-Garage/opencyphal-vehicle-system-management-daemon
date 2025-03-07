//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_SVC_NODE_ACCESS_REGISTERS_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_SVC_NODE_ACCESS_REGISTERS_CLIENT_HPP_INCLUDED

#include "ipc/client_router.hpp"
#include "ocvsmd/sdk/node_registry_client.hpp"
#include "svc/node/access_registers_spec.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <memory>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace node
{

/// Defines interface of the 'Node: Access Registers' service client.
///
class AccessRegistersClient
{
public:
    using Ptr  = std::shared_ptr<AccessRegistersClient>;
    using Spec = common::svc::node::AccessRegistersSpec;

    using Result  = NodeRegistryClient::Access::Result;
    using Success = NodeRegistryClient::Access::Success;
    using Failure = NodeRegistryClient::Access::Failure;

    CETL_NODISCARD static Ptr make(cetl::pmr::memory_resource&               memory,
                                   const common::ipc::ClientRouter::Ptr&     ipc_router,
                                   const cetl::span<const std::uint16_t>     node_ids,
                                   const cetl::span<const cetl::string_view> registers,
                                   const std::chrono::microseconds           timeout);

    AccessRegistersClient(AccessRegistersClient&&)                 = delete;
    AccessRegistersClient(const AccessRegistersClient&)            = delete;
    AccessRegistersClient& operator=(AccessRegistersClient&&)      = delete;
    AccessRegistersClient& operator=(const AccessRegistersClient&) = delete;

    virtual ~AccessRegistersClient() = default;

    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
        submitImpl([receive = std::forward<Receiver>(receiver)](Result&& result) mutable {
            //
            receive(std::move(result));
        });
    }

protected:
    AccessRegistersClient() = default;

    virtual void submitImpl(std::function<void(Result&&)>&& receiver) = 0;

};  // AccessRegistersClient

}  // namespace node
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_NODE_ACCESS_REGISTERS_CLIENT_HPP_INCLUDED
