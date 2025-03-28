//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "pop_root_client.hpp"

#include "ipc/channel.hpp"
#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "svc/file_server/pop_root_spec.hpp"

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

/// Defines implementation of the 'File Server: Pop Root' service client.
///
class PopRootClientImpl final : public PopRootClient
{
public:
    PopRootClientImpl(cetl::pmr::memory_resource&           memory,
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

        channel_.subscribe([this](const auto& event_var, const auto) {
            //
            cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
        });
    }

private:
    using Channel = common::ipc::Channel<Spec::Response, Spec::Request>;

    void handleEvent(const Channel::Connected& connected)
    {
        logger_->trace("PopRootClient::handleEvent({}).", connected);

        if (const auto opt_error = channel_.send(request_))
        {
            CETL_DEBUG_ASSERT(receiver_, "");

            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input&)
    {
        logger_->trace("PopRootClient::handleEvent(Input).");
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        receiver_(completed.opt_error ? Result{Failure{*completed.opt_error}} : Success{});
    }

    cetl::pmr::memory_resource&   memory_;
    common::LoggerPtr             logger_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;

};  // PopRootClientImpl

}  // namespace

PopRootClient::Ptr PopRootClient::make(  //
    cetl::pmr::memory_resource&           memory,
    const common::ipc::ClientRouter::Ptr& ipc_router,
    const Spec::Request&                  request)
{
    return std::make_shared<PopRootClientImpl>(memory, ipc_router, request);
}

}  // namespace file_server
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
