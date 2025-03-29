//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include <ocvsmd/sdk/daemon.hpp>

#include "ipc/channel.hpp"
#include "ipc/client_router.hpp"
#include "ipc/pipe/client_pipe.hpp"
#include "ipc/pipe/socket_client.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/node_command_client.hpp"
#include "ocvsmd/sdk/node_registry_client.hpp"
#include "sdk_factory.hpp"
#include "svc/as_sender.hpp"
#include "svc/relay/raw_publisher_client.hpp"
#include "svc/relay/raw_publisher_spec.hpp"
#include "svc/relay/raw_subscriber_client.hpp"
#include "svc/relay/raw_subscriber_spec.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/executor.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace
{

class DaemonImpl final : public Daemon
{
public:
    DaemonImpl(cetl::pmr::memory_resource& memory, libcyphal::IExecutor& executor)
        : memory_{memory}
        , executor_{executor}
        , logger_{common::getLogger("sdk")}
    {
    }

    CETL_NODISCARD OptError start(const std::string& connection)
    {
        logger_->info("Starting with IPC connection '{}'...", connection);

        common::ipc::pipe::ClientPipe::Ptr client_pipe;
        {
            using ParseResult = common::io::SocketAddress::ParseResult;

            auto maybe_socket_address = common::io::SocketAddress::parse(connection, 0);
            if (const auto* const failure = cetl::get_if<ParseResult::Failure>(&maybe_socket_address))
            {
                logger_->error("Failed to parse IPC connection string ('{}', err={}).", connection, *failure);
                return OptError{*failure};
            }
            const auto socket_address = cetl::get<ParseResult::Success>(maybe_socket_address);
            client_pipe               = std::make_unique<common::ipc::pipe::SocketClient>(executor_, socket_address);
        }

        ipc_router_ = common::ipc::ClientRouter::make(memory_, std::move(client_pipe));

        file_server_          = Factory::makeFileServer(memory_, ipc_router_);
        node_command_client_  = Factory::makeNodeCommandClient(memory_, ipc_router_);
        node_registry_client_ = Factory::makeNodeRegistryClient(memory_, ipc_router_);

        if (const auto opt_error = ipc_router_->start())
        {
            logger_->error("Failed to start IPC router (err={}).", *opt_error);
            return opt_error;
        }

        logger_->debug("Started IPC connection.");
        return OptError{};
    }

    // Daemon

    FileServer::Ptr getFileServer() const override
    {
        return file_server_;
    }

    NodeCommandClient::Ptr getNodeCommandClient() const override
    {
        return node_command_client_;
    }

    NodeRegistryClient::Ptr getNodeRegistryClient() const override
    {
        return node_registry_client_;
    }

    SenderOf<MakeRawPublisher::Result>::Ptr makeRawPublisher(const CyphalPortId subject_id) override
    {
        using RawPublisherClient = svc::relay::RawPublisherClient;
        using Request            = common::svc::relay::RawPublisherSpec::Request;

        logger_->trace("Making sender of `makeRawPublisher()`.");

        Request request{&memory_};
        auto&   create_req    = request.set_create();
        create_req.subject_id = subject_id;
        auto svc_client       = RawPublisherClient::make(memory_, ipc_router_, request);

        return std::make_unique<svc::AsSender<MakeRawPublisher::Result, decltype(svc_client)>>(  //
            "Daemon::makeRawPublisher",
            std::move(svc_client),
            logger_);
    }

    SenderOf<MakeRawSubscriber::Result>::Ptr makeRawSubscriber(  //
        const CyphalPortId subject_id,                           // NOLINT bugprone-easily-swappable-parameters
        const std::size_t  extent_bytes) override
    {
        using RawSubscriberClient = svc::relay::RawSubscriberClient;
        using Request             = common::svc::relay::RawSubscriberSpec::Request;

        logger_->trace("Making sender of `makeRawSubscriber()`.");

        Request request{&memory_};
        auto&   create_req     = request.set_create();
        create_req.subject_id  = subject_id;
        create_req.extent_size = extent_bytes;
        auto svc_client        = RawSubscriberClient::make(memory_, ipc_router_, request);

        return std::make_unique<svc::AsSender<MakeRawSubscriber::Result, decltype(svc_client)>>(  //
            "Daemon::makeRawSubscriber",
            std::move(svc_client),
            logger_);
    }

private:
    cetl::pmr::memory_resource&    memory_;
    libcyphal::IExecutor&          executor_;
    common::LoggerPtr              logger_;
    common::ipc::ClientRouter::Ptr ipc_router_;
    FileServer::Ptr                file_server_;
    NodeCommandClient::Ptr         node_command_client_;
    NodeRegistryClient::Ptr        node_registry_client_;

};  // DaemonImpl

}  // namespace

CETL_NODISCARD Daemon::Ptr Daemon::make(cetl::pmr::memory_resource& memory,
                                        libcyphal::IExecutor&       executor,
                                        const std::string&          connection)
{
    auto daemon = std::make_shared<DaemonImpl>(memory, executor);
    if (const auto opt_error = daemon->start(connection))
    {
        (void) opt_error;
        return nullptr;
    }

    return daemon;
}

}  // namespace sdk
}  // namespace ocvsmd
