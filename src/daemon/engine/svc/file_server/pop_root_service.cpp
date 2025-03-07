//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "pop_root_service.hpp"

#include "cyphal/file_provider.hpp"
#include "ipc/channel.hpp"
#include "ipc/server_router.hpp"
#include "logging.hpp"
#include "svc/file_server/pop_root_spec.hpp"
#include "svc/svc_helpers.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <memory>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace svc
{
namespace file_server
{
namespace
{

/// Defines 'File Server: Pop Root' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class PopRootServiceImpl final
{
public:
    using Spec    = common::svc::file_server::PopRootSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit PopRootServiceImpl(const ScvContext& context, cyphal::FileProvider& file_provider)
        : context_{context}
        , file_provider_{file_provider}
    {
    }

    /// Handles the initial `file_server::PopRoot` service request of a new IPC channel.
    ///
    /// The service itself is stateless (the state is stored inside the given file provider),
    /// has no async operations, and completes the channel immediately.
    ///
    /// Defined as a functor operator - as it's required/expected by the IPC server router.
    ///
    void operator()(Channel channel, const Spec::Request& request) const
    {
        logger_->debug("New '{}' service channel.", Spec::svc_full_name());

        std::string path;
        std::copy(request.item.path.cbegin(), request.item.path.cend(), std::back_inserter(path));
        file_provider_.popRoot(path, request.is_back);

        if (const auto err = channel.complete())
        {
            logger_->warn("PopRootSvc: failed to send ipc completion (err={}).", err);
        }
    }

private:
    const ScvContext      context_;
    cyphal::FileProvider& file_provider_;
    common::LoggerPtr     logger_{common::getLogger("engine")};

};  // PopRootServiceImpl

}  // namespace

void PopRootService::registerWithContext(const ScvContext& context, cyphal::FileProvider& file_provider)
{
    using Impl = PopRootServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context, file_provider});
}

}  // namespace file_server
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
