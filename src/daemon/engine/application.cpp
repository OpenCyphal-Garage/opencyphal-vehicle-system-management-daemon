//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "application.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/application/node.hpp>
#include <libcyphal/types.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <utility>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{

cetl::optional<std::string> Application::init()
{
    // 1. Create the transport layer object.
    //
    auto* const transport_iface = udp_transport_bag_.create();
    if (transport_iface == nullptr)
    {
        return "Failed to create cyphal UDP transport.";
    }

    // 2. Create the presentation layer object.
    //
    presentation_.emplace(memory_, executor_, *transport_iface);

    // 3. Create the node object with name.
    //
    auto maybe_node = libcyphal::application::Node::make(presentation_.value());
    if (const auto* failure = cetl::get_if<libcyphal::application::Node::MakeFailure>(&maybe_node))
    {
        (void) failure;
        return "Failed to create cyphal node.";
    }
    node_.emplace(cetl::get<libcyphal::application::Node>(std::move(maybe_node)));

    // 4. Populate the node info.
    //
    auto& get_info_prov = node_->getInfoProvider();
    get_info_prov  //
        .setName(NODE_NAME)
        .setSoftwareVersion(VERSION_MAJOR, VERSION_MINOR)
        .setSoftwareVcsRevisionId(VCS_REVISION_ID)
        .setUniqueId(getUniqueId());

    if (!ipc_server_.start())
    {
        return "Failed to start IPC server.";
    }

    ipc_server_callback_ = ipc_server_.registerListenCallback([this](const auto&) {
        //
        ipc_server_.accept();
    });

    return cetl::nullopt;
}

void Application::runWhile(const std::function<bool()>& loop_predicate)
{
    using std::chrono_literals::operator""s;

    while (loop_predicate())
    {
        const auto spin_result = executor_.spinOnce();

        // Poll awaitable resources but awake at least once per second.
        libcyphal::Duration timeout{1s};
        if (spin_result.next_exec_time.has_value())
        {
            timeout = std::min(timeout, spin_result.next_exec_time.value() - executor_.now());
        }

        // TODO: Don't ignore polling failures; come up with a strategy to handle them.
        // Probably we should log it, break the loop,
        // and exit with a failure code (b/c it is a critical and unexpected error).
        auto maybe_poll_failure = executor_.pollAwaitableResourcesFor(cetl::make_optional(timeout));
        (void) maybe_poll_failure;
    }
}

Application::UniqueId Application::getUniqueId()
{
    UniqueId out_unique_id = {};

    // TODO: add storage for the unique ID

    std::random_device                          rd;         // Seed for the random number engine
    std::mt19937                                gen{rd()};  // Mersenne Twister engine
    std::uniform_int_distribution<std::uint8_t> dis{std::numeric_limits<std::uint8_t>::min(),
                                                    std::numeric_limits<std::uint8_t>::max()};

    // Populate the default; it is only used at the first run.
    for (auto& b : out_unique_id)
    {
        b = dis(gen);
    }

    return out_unique_id;
}

}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
