//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "list_roots_service.hpp"

#include "cyphal/file_provider.hpp"
#include "ipc/channel.hpp"
#include "ipc/server_router.hpp"
#include "logging.hpp"
#include "svc/file_server/list_roots_spec.hpp"
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

/// Defines 'File Server: List Roots' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class ListRootsServiceImpl final
{
public:
    using Spec    = common::svc::file_server::ListRootsSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit ListRootsServiceImpl(const ScvContext& context, cyphal::FileProvider& file_provider)
        : context_{context}
        , file_provider_{file_provider}
    {
    }

    /// Handles the `file_server::ListRoots` service request of a new IPC channel.
    ///
    /// The service itself is stateless (the state is stored inside the given file provider), has no async operations,
    /// sends multiple responses (per each root), and then completes the channel immediately.
    ///
    /// Defined as a functor operator - as it's required/expected by the IPC server router.
    ///
    void operator()(Channel channel, const Spec::Request&) const
    {
        logger_->debug("New '{}' service channel.", Spec::svc_full_name());

        Spec::Response ipc_response{&context_.memory};
        const auto&    roots = file_provider_.getListOfRoots();
        for (const auto& root : roots)
        {
            // Check if a root path is too long. Such paths could be read from the configuration file -
            // they will work fine in terms of the file system, but we can't send them over IPC.
            //
            constexpr auto MaxRootLen = Spec::Response::_traits_::TypeOf::item::_traits_::ArrayCapacity::path;
            if (root.size() > MaxRootLen)
            {
                logger_->warn("ListRootsSvc: Can't list too long path (max_len={}, root='{}').", MaxRootLen, root);
                continue;
            }

            ipc_response.item.path.clear();
            std::copy(root.cbegin(), root.cend(), std::back_inserter(ipc_response.item.path));
            if (const auto err = channel.send(ipc_response))
            {
                logger_->warn("ListRootsSvc: failed to send ipc response (err={}).", err);
            }
        }

        if (const auto err = channel.complete())
        {
            logger_->warn("ListRootsSvc: failed to send ipc completion (err={}).", err);
        }
    }

private:
    const ScvContext      context_;
    cyphal::FileProvider& file_provider_;
    common::LoggerPtr     logger_{common::getLogger("engine")};

};  // ListRootsServiceImpl

}  // namespace

void ListRootsService::registerWithContext(const ScvContext& context, cyphal::FileProvider& file_provider)
{
    using Impl = ListRootsServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context, file_provider});
}

}  // namespace file_server
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
