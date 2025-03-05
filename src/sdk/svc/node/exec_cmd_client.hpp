//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_SVC_NODE_EXEC_CMD_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_SVC_NODE_EXEC_CMD_CLIENT_HPP_INCLUDED

#include "ipc/client_router.hpp"
#include "svc/node/exec_cmd_spec.hpp"

#include <uavcan/node/ExecuteCommand_1_3.hpp>

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <memory>
#include <unordered_map>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace node
{

/// Defines interface of the 'Node: Exec Command' service client.
///
class ExecCmdClient
{
public:
    using Ptr          = std::shared_ptr<ExecCmdClient>;
    using Spec         = common::svc::node::ExecCmdSpec;
    using NodeResponse = uavcan::node::ExecuteCommand_1_3::Response;

    using Success = std::unordered_map<std::uint16_t, NodeResponse>;
    using Failure = int;  // `errno`-like error code
    using Result  = cetl::variant<Success, Failure>;

    CETL_NODISCARD static Ptr make(cetl::pmr::memory_resource&           memory,
                                   const common::ipc::ClientRouter::Ptr& ipc_router,
                                   Spec::Request&&                       request);

    ExecCmdClient(ExecCmdClient&&)                 = delete;
    ExecCmdClient(const ExecCmdClient&)            = delete;
    ExecCmdClient& operator=(ExecCmdClient&&)      = delete;
    ExecCmdClient& operator=(const ExecCmdClient&) = delete;

    virtual ~ExecCmdClient() = default;

    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
        submitImpl([receive = std::forward<Receiver>(receiver)](Result&& result) mutable {
            //
            receive(std::move(result));
        });
    }

protected:
    ExecCmdClient() = default;

    virtual void submitImpl(std::function<void(Result&&)>&& receiver) = 0;

};  // ExecCmdClient

}  // namespace node
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_NODE_EXEC_CMD_CLIENT_HPP_INCLUDED
