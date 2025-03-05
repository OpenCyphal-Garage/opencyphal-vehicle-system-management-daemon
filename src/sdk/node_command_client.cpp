//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include <ocvsmd/sdk/node_command_client.hpp>

#include "ipc/channel.hpp"
#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/execution.hpp"
#include "sdk_factory.hpp"
#include "svc/node/exec_cmd_client.hpp"
#include "svc/node/exec_cmd_spec.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace
{

class NodeCommandClientImpl final : public NodeCommandClient
{
public:
    NodeCommandClientImpl(cetl::pmr::memory_resource& memory, common::ipc::ClientRouter::Ptr ipc_router)
        : memory_{memory}
        , ipc_router_{std::move(ipc_router)}
        , logger_{common::getLogger("sdk")}
    {
    }

    // NodeCommandClient

    cetl::pmr::memory_resource& getMemoryResource() const noexcept override
    {
        return memory_;
    }

    SenderOf<Command::Result>::Ptr sendCommand(const cetl::span<const std::uint16_t> node_ids,
                                               const Command::NodeRequest&           node_request,
                                               const std::chrono::microseconds       timeout) override
    {
        common::svc::node::ExecCmdSpec::Request request{std::max<std::uint64_t>(0, timeout.count()),
                                                        {node_ids.begin(), node_ids.end(), &memory_},
                                                        {node_request.command, node_request.parameter, &memory_},
                                                        &memory_};

        auto svc_client = ExecCmdClient::make(memory_, ipc_router_, std::move(request));

        return std::make_unique<CommandSender>(std::move(svc_client));
    }

private:
    using ExecCmdClient = svc::node::ExecCmdClient;

    class CommandSender final : public SenderOf<Command::Result>
    {
    public:
        explicit CommandSender(ExecCmdClient::Ptr svc_client)
            : svc_client_{std::move(svc_client)}
        {
        }

        void submitImpl(std::function<void(Command::Result&&)>&& receiver) override
        {
            svc_client_->submit([receiver = std::move(receiver)](ExecCmdClient::Result&& result) mutable {
                //
                receiver(cetl::visit(
                    [](auto&& value) {
                        //
                        return transform(std::forward<decltype(value)>(value));
                    },
                    std::move(result)));
            });
        }

    private:
        static Command::Result transform(ExecCmdClient::Success&& svc_success)
        {
            Command::Success cmd_success{};
            cmd_success.reserve(svc_success.size());
            for (auto&& pair : std::move(svc_success))
            {
                cmd_success.emplace(pair.first, std::move(pair.second));
            }
            return cmd_success;
        }

        static Command::Result transform(ExecCmdClient::Failure failure)
        {
            return Command::Failure{failure};
        }

        ExecCmdClient::Ptr svc_client_;

    };  // CommandSender

    cetl::pmr::memory_resource&    memory_;
    common::LoggerPtr              logger_;
    common::ipc::ClientRouter::Ptr ipc_router_;

};  // NodeCommandClientImpl

}  // namespace

SenderOf<NodeCommandClient::Command::Result>::Ptr NodeCommandClient::restart(  //
    const cetl::span<const std::uint16_t> node_ids,
    const std::chrono::microseconds       timeout)
{
    auto& memory = getMemoryResource();

    constexpr auto                                          command = Command::NodeRequest::COMMAND_RESTART;
    const Command::NodeRequest::_traits_::TypeOf::parameter no_param{&memory};
    const Command::NodeRequest                              node_request{command, no_param, {&memory}};

    return sendCommand(node_ids, node_request, timeout);
}

SenderOf<NodeCommandClient::Command::Result>::Ptr NodeCommandClient::beginSoftwareUpdate(  //
    const cetl::span<const std::uint16_t> node_ids,
    const cetl::string_view               file_path,
    const std::chrono::microseconds       timeout)
{
    using Parameter = Command::NodeRequest::_traits_::TypeOf::parameter;

    auto&           memory  = getMemoryResource();
    constexpr auto  command = Command::NodeRequest::COMMAND_BEGIN_SOFTWARE_UPDATE;
    const Parameter param{file_path.begin(), file_path.end(), &memory};

    const Command::NodeRequest node_request{command, param, {&memory}};

    return sendCommand(node_ids, node_request, timeout);
}

CETL_NODISCARD NodeCommandClient::Ptr Factory::makeNodeCommandClient(cetl::pmr::memory_resource&    memory,
                                                                     common::ipc::ClientRouter::Ptr ipc_router)
{
    return std::make_shared<NodeCommandClientImpl>(memory, std::move(ipc_router));
}

}  // namespace sdk
}  // namespace ocvsmd
