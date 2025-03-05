//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "push_root_client.hpp"

#include "ipc/channel.hpp"
#include "ipc/client_router.hpp"
#include "ipc/ipc_types.hpp"
#include "logging.hpp"
#include "svc/file_server/push_root_spec.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace file_server
{
namespace
{

/// Defines implementation of the 'File Server: Push Root' service client.
///
class PushRootClientImpl final : public PushRootClient
{
public:
    PushRootClientImpl(cetl::pmr::memory_resource&           memory,
                       const common::ipc::ClientRouter::Ptr& ipc_router,
                       Spec::Request                         request)
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
        logger_->trace("PushRootClient::handleEvent({}).", connected);

        if (const auto err = channel_.send(request_))
        {
            CETL_DEBUG_ASSERT(receiver_, "");

            receiver_(Failure{err});
        }
    }

    void handleEvent(const Channel::Input&)
    {
        logger_->trace("PushRootClient::handleEvent(Input).");
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        if (completed.error_code != common::ipc::ErrorCode::Success)
        {
            receiver_(static_cast<Failure>(completed.error_code));
            return;
        }
        receiver_({});
    }

    cetl::pmr::memory_resource&   memory_;
    common::LoggerPtr             logger_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;

};  // PushRootClientImpl

}  // namespace

CETL_NODISCARD PushRootClient::Ptr PushRootClient::make(cetl::pmr::memory_resource&           memory,
                                                        const common::ipc::ClientRouter::Ptr& ipc_router,
                                                        const Spec::Request&                  request)
{
    return std::make_shared<PushRootClientImpl>(memory, ipc_router, request);
}

}  // namespace file_server
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
