//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "exec_cmd_client.hpp"

#include "ipc/channel.hpp"
#include "ipc/client_router.hpp"
#include "ipc/ipc_types.hpp"
#include "logging.hpp"
#include "svc/node/exec_cmd_spec.hpp"

#include <uavcan/node/ExecuteCommand_1_3.hpp>

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <functional>
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
namespace
{

class ExecCmdClientImpl final : public ExecCmdClient
{
public:
    ExecCmdClientImpl(cetl::pmr::memory_resource&           memory,
                      const common::ipc::ClientRouter::Ptr& ipc_router,
                      Spec::Request&&                       request)
        : memory_{memory}
        , logger_{common::getLogger("svc")}
        , request_{std::move(request)}
        , channel_{ipc_router->makeChannel<Channel>(Spec::svc_full_name())}
    {
    }

    void submitImpl(std::function<void(Result&&)>&& receiver) override
    {
        receiver_ = std::move(receiver);

        channel_.subscribe([this](const auto& event_var) {
            //
            cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
        });
    }

private:
    using Channel = common::ipc::Channel<Spec::Response, Spec::Request>;

    void handleEvent(const Channel::Connected& connected)
    {
        logger_->trace("ExecCmdClient::handleEvent({}).", connected);

        if (const auto err = channel_.send(request_))
        {
            CETL_DEBUG_ASSERT(receiver_, "");

            receiver_(Failure{err});
        }
    }

    void handleEvent(const Channel::Input& input)
    {
        logger_->trace("ExecCmdClient::handleEvent(Input).");

        NodeResponse node_response{input.payload.status, input.payload.output, &memory_};
        node_id_to_response_.emplace(input.node_id, std::move(node_response));
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->debug("ExecCmdClient::handleEvent({}).", completed);

        if (completed.error_code != common::ipc::ErrorCode::Success)
        {
            receiver_(static_cast<Failure>(completed.error_code));
            return;
        }
        receiver_(std::move(node_id_to_response_));
    }

    cetl::pmr::memory_resource&   memory_;
    common::LoggerPtr             logger_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;
    Success                       node_id_to_response_;

};  // ExecCmdClientImpl

}  // namespace

ExecCmdClient::Ptr ExecCmdClient::make(  //
    cetl::pmr::memory_resource&           memory,
    const common::ipc::ClientRouter::Ptr& ipc_router,
    Spec::Request&&                       request)
{
    return std::make_shared<ExecCmdClientImpl>(memory, ipc_router, std::move(request));
}

}  // namespace node
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
