//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_SVC_NODE_LIST_REGISTERS_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_SVC_NODE_LIST_REGISTERS_CLIENT_HPP_INCLUDED

#include "ipc/client_router.hpp"
#include "ocvsmd/sdk/node_registry_client.hpp"
#include "svc/node/list_registers_spec.hpp"

#include <cetl/cetl.hpp>

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

/// Defines interface of the 'Node: List Registers' service client.
///
class ListRegistersClient
{
public:
    using Ptr  = std::shared_ptr<ListRegistersClient>;
    using Spec = common::svc::node::ListRegistersSpec;

    using Result  = NodeRegistryClient::List::Result;
    using Success = NodeRegistryClient::List::Success;
    using Failure = NodeRegistryClient::List::Failure;

    CETL_NODISCARD static Ptr make(const common::ipc::ClientRouter::Ptr& ipc_router, Spec::Request&& request);

    ListRegistersClient(ListRegistersClient&&)                 = delete;
    ListRegistersClient(const ListRegistersClient&)            = delete;
    ListRegistersClient& operator=(ListRegistersClient&&)      = delete;
    ListRegistersClient& operator=(const ListRegistersClient&) = delete;

    virtual ~ListRegistersClient() = default;

    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
        submitImpl([receive = std::forward<Receiver>(receiver)](Result&& result) mutable {
            //
            receive(std::move(result));
        });
    }

protected:
    ListRegistersClient() = default;

    virtual void submitImpl(std::function<void(Result&&)>&& receiver) = 0;

};  // ListRegistersClient

}  // namespace node
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_NODE_LIST_REGISTERS_CLIENT_HPP_INCLUDED
