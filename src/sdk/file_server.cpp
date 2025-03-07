//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include <ocvsmd/sdk/file_server.hpp>

#include "svc/as_sender.hpp"

#include "ipc/channel.hpp"
#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "sdk_factory.hpp"
#include "svc/file_server/list_roots_client.hpp"
#include "svc/file_server/list_roots_spec.hpp"
#include "svc/file_server/pop_root_client.hpp"
#include "svc/file_server/pop_root_spec.hpp"
#include "svc/file_server/push_root_client.hpp"
#include "svc/file_server/push_root_spec.hpp"

#include <string>

namespace ocvsmd
{
namespace sdk
{
namespace
{

class FileServerImpl final : public FileServer
{
public:
    FileServerImpl(cetl::pmr::memory_resource& memory, common::ipc::ClientRouter::Ptr ipc_router)
        : memory_{memory}
        , ipc_router_{std::move(ipc_router)}
        , logger_{common::getLogger("sdk")}
    {
    }

    // FileServer

    SenderOf<ListRoots::Result>::Ptr listRoots() override
    {
        using ListRootsClient = svc::file_server::ListRootsClient;
        using Request         = common::svc::file_server::ListRootsSpec::Request;

        logger_->trace("FileServer: Making sender of `listRoots()`.");

        const Request request{&memory_};
        auto          svc_client = ListRootsClient::make(memory_, ipc_router_, request);

        return std::make_unique<svc::AsSender<ListRootsClient, ListRoots::Result>>(  //
            "FileServer::listRoots",
            std::move(svc_client),
            logger_);
    }

    SenderOf<PopRoot::Result>::Ptr popRoot(const std::string& path, const bool back) override
    {
        using PopRootClient = svc::file_server::PopRootClient;
        using Request       = common::svc::file_server::PopRootSpec::Request;

        logger_->trace("FileServer: Making sender of `popRoot(path='{}', back={})`.", path, back);

        if (path.size() > Request::_traits_::TypeOf::item::_traits_::ArrayCapacity::path)
        {
            logger_->error("Too long path '{}'.", path);
            return just<PopRoot::Result>(EINVAL);
        }

        common::svc::file_server::PopRootSpec::Request request{&memory_};
        std::copy(path.cbegin(), path.cend(), std::back_inserter(request.item.path));
        request.is_back = back;
        auto svc_client = PopRootClient::make(memory_, ipc_router_, request);

        return std::make_unique<svc::AsSender<PopRootClient, PopRoot::Result>>(  //
            "FileServer::popRoot",
            std::move(svc_client),
            logger_);
    }

    SenderOf<PushRoot::Result>::Ptr pushRoot(const std::string& path, const bool back) override
    {
        using PushRootClient = svc::file_server::PushRootClient;
        using Request        = common::svc::file_server::PushRootSpec::Request;

        logger_->trace("FileServer: Making sender of `pushRoot(path='{}', back={})`.", path, back);

        if (path.size() > Request::_traits_::TypeOf::item::_traits_::ArrayCapacity::path)
        {
            logger_->error("Too long path '{}'.", path);
            return just<PopRoot::Result>(EINVAL);
        }

        Request request{&memory_};
        std::copy(path.cbegin(), path.cend(), std::back_inserter(request.item.path));
        request.is_back = back;
        auto svc_client = PushRootClient::make(memory_, ipc_router_, request);

        return std::make_unique<svc::AsSender<PushRootClient, PushRoot::Result>>(  //
            "FileServer::pushRoot",
            std::move(svc_client),
            logger_);
    }

private:
    cetl::pmr::memory_resource&    memory_;
    common::LoggerPtr              logger_;
    common::ipc::ClientRouter::Ptr ipc_router_;

};  // FileServerImpl

}  // namespace

CETL_NODISCARD FileServer::Ptr Factory::makeFileServer(cetl::pmr::memory_resource&    memory,
                                                       common::ipc::ClientRouter::Ptr ipc_router)
{
    return std::make_shared<FileServerImpl>(memory, std::move(ipc_router));
}

}  // namespace sdk
}  // namespace ocvsmd
