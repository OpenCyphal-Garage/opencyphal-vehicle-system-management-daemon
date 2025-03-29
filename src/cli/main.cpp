//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "dsdl_helpers.hpp"
#include "fmt_helpers.hpp"
#include "setup_logging.hpp"

#include <ocvsmd/platform/defines.hpp>
#include <ocvsmd/sdk/daemon.hpp>
#include <ocvsmd/sdk/defines.hpp>
#include <ocvsmd/sdk/execution.hpp>
#include <ocvsmd/sdk/node_command_client.hpp>

#include <uavcan/node/Heartbeat_1_0.hpp>
#include <uavcan/time/Synchronization_1_0.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <signal.h>  // NOLINT
#include <set>
#include <unistd.h>
#include <utility>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

namespace
{

using ocvsmd::sdk::Error;
using ocvsmd::sdk::OptError;
using ocvsmd::sdk::Daemon;
using ocvsmd::sdk::sync_wait;
using Executor = ocvsmd::platform::SingleThreadedExecutor;

using std::literals::chrono_literals::operator""s;  // NOLINT(misc-unused-using-decls)

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile sig_atomic_t g_running = 1;

void signalHandler(const int sig)
{
    switch (sig)
    {
    case SIGINT:
    case SIGTERM:
        g_running = 0;
        break;
    default:
        break;
    }
}

void setupSignalHandlers()
{
    struct sigaction sigbreak
    {};
    sigbreak.sa_handler = &signalHandler;
    ::sigaction(SIGINT, &sigbreak, nullptr);
    ::sigaction(SIGTERM, &sigbreak, nullptr);
}

void setRegisterValue(ocvsmd::sdk::NodeRegistryClient::Access::RegValue& reg_val, const std::string& text)
{
    auto& reg_str = reg_val.set_string();
    std::copy(text.begin(), text.end(), std::back_inserter(reg_str.value));
}

// MARK: - Logging helpers:

void logCommandResult(const ocvsmd::sdk::NodeCommandClient::Command::Result& cmd_result)
{
    using Command = ocvsmd::sdk::NodeCommandClient::Command;

    if (const auto* const failure = cetl::get_if<Command::Failure>(&cmd_result))
    {
        spdlog::error("Failed to send command (err={}).", *failure);
        return;
    }

    const auto& responds = cetl::get<Command::Success>(cmd_result);
    for (const auto& node_and_respond : responds)
    {
        if (const auto* const failure = cetl::get_if<1>(&node_and_respond.second))
        {
            spdlog::warn("{:4} → err={}", node_and_respond.first, *failure);
            continue;
        }
        const auto& response = cetl::get<0>(node_and_respond.second);
        spdlog::info("{:4} → status={}.", node_and_respond.first, response.status);
    }
}

void logRegistryListNodeResult(  //
    std::set<std::string>&                                              reg_names_set,
    const ocvsmd::sdk::CyphalNodeId                                     node_id,
    const ocvsmd::sdk::NodeRegistryClient::List::NodeRegisters::Result& result)
{
    using NodeRegisters = ocvsmd::sdk::NodeRegistryClient::List::NodeRegisters;

    if (const auto* const failure = cetl::get_if<NodeRegisters::Failure>(&result))
    {
        spdlog::warn("{:4} → err={}", node_id, *failure);
        return;
    }

    const auto& node_regs = cetl::get<NodeRegisters::Success>(result);
    for (const auto& reg_name : node_regs)
    {
        reg_names_set.insert(reg_name);
        spdlog::info("{:4} → '{}'", node_id, reg_name);
    }
}

void logRegistryAccessNodeResult(const ocvsmd::sdk::CyphalNodeId                                       node_id,
                                 const ocvsmd::sdk::NodeRegistryClient::Access::NodeRegisters::Result& result)
{
    using NodeRegs = ocvsmd::sdk::NodeRegistryClient::Access::NodeRegisters;

    if (const auto* const failure = cetl::get_if<NodeRegs::Failure>(&result))
    {
        spdlog::warn("{:4} → err={}", node_id, *failure);
        return;
    }
    const auto& node_reg_vals = cetl::get<NodeRegs::Success>(result);

    for (const auto& reg_key_val : node_reg_vals)
    {
        if (const auto* const failure = cetl::get_if<1>(&reg_key_val.value_or_err))
        {
            spdlog::warn("{:4} → '{}' err={}", node_id, reg_key_val.key, *failure);
            continue;
        }
        const auto& reg_val = cetl::get<0>(reg_key_val.value_or_err);

        spdlog::info("{:4} → '{}'={}", node_id, reg_key_val.key, reg_val);
    }
}

void logRegistryAccessResult(ocvsmd::sdk::NodeRegistryClient::Access::Result&& result)
{
    using Access = ocvsmd::sdk::NodeRegistryClient::Access;

    const auto node_id_to_reg_vals = cetl::get<Access::Success>(std::move(result));
    spdlog::info("Engine responded with list of nodes (cnt={}):", node_id_to_reg_vals.size());
    for (const auto& id_and_reg_vals : node_id_to_reg_vals)
    {
        logRegistryAccessNodeResult(id_and_reg_vals.first, id_and_reg_vals.second);
    }
}

void logHeartbeat(const uavcan::node::Heartbeat_1_0&               hb,
                  const cetl::optional<ocvsmd::sdk::CyphalNodeId>& opt_node_id,
                  const ocvsmd::sdk::CyphalPriority                priority)
{
    if (const auto node_id = opt_node_id)
    {
        spdlog::info(  //
            "❤️ heartbeat from {} node: uptime={}s, health={}, mode={}, vendor_status={} (cy_priority={}).",
            *node_id,
            hb.uptime,
            static_cast<int>(hb.health.value),
            static_cast<int>(hb.mode.value),
            static_cast<int>(hb.vendor_specific_status_code),
            static_cast<int>(priority));
    }
    else
    {
        spdlog::info(  //
            "🖤 heartbeat from anonymous node: uptime={}s, health={}, mode={}, vendor_status={} (cy_priority={}).",
            hb.uptime,
            static_cast<int>(hb.health.value),
            static_cast<int>(hb.mode.value),
            static_cast<int>(hb.vendor_specific_status_code),
            static_cast<int>(priority));
    }
}

// MARK: - Test Scenarios:

/// Demo of daemon's node command client - sending `COMMAND_RESTART` command to nodes: 42, 43, & 44.
///
void tryResetNodesScenario(Executor& executor, const Daemon::Ptr& daemon)
{
    using Command = ocvsmd::sdk::NodeCommandClient::Command;

    spdlog::info("tryResetNodesScenario -----------------");

    const auto node_cmd_client = daemon->getNodeCommandClient();

    std::array<ocvsmd::sdk::CyphalNodeId, 3> node_ids{42, 43, 44};
    auto                                     sender     = node_cmd_client->restart(node_ids);
    const auto                               cmd_result = sync_wait<Command::Result>(executor, std::move(sender), 2s);
    logCommandResult(cmd_result);

    // Let child nodes time (100 ms) to restart.
    ::usleep(100'000);  // TODO: Add `.delay(duration)` execution operator.
}

/// Demo of daemon's node command client - sending `COMMAND_BEGIN_SOFTWARE_UPDATE` command to nodes: 42, 43, & 44.
///
void tryBeginSoftwareUpdateScenario(Executor& executor, const Daemon::Ptr& daemon)
{
    using Command = ocvsmd::sdk::NodeCommandClient::Command;

    spdlog::info("tryBeginSoftwareUpdateScenario -----------------");

    const auto node_cmd_client = daemon->getNodeCommandClient();

    std::array<ocvsmd::sdk::CyphalNodeId, 3> node_ids{42, 43, 44};
    auto                                     sender = node_cmd_client->beginSoftwareUpdate(node_ids, "firmware.bin");
    const auto                               cmd_result = sync_wait<Command::Result>(executor, std::move(sender), 2s);
    logCommandResult(cmd_result);
}

/// Demo of daemon's file server - push root.
///
void tryPushRootScenario(Executor& executor, const Daemon::Ptr& daemon)
{
    using PushRoot = ocvsmd::sdk::FileServer::PushRoot;

    spdlog::info("tryPushRootScenario -----------------");

    const auto file_server = daemon->getFileServer();

    const std::string path{"key"};
    auto              sender     = file_server->pushRoot(path, true);
    auto              cmd_result = sync_wait<PushRoot::Result>(executor, std::move(sender), 2s);
    if (const auto* const failure = cetl::get_if<PushRoot::Failure>(&cmd_result))
    {
        spdlog::error("Failed to push FS root (path='{}', err={}).", path, *failure);
    }
    else
    {
        spdlog::info("File Server responded ok on 'PushRoot'.");
    }
}

/// Demo of daemon's file server - pop root.
///
void tryPopRootScenario(Executor& executor, const Daemon::Ptr& daemon)
{
    using PopRoot = ocvsmd::sdk::FileServer::PopRoot;

    spdlog::info("tryPopRootScenario -----------------");

    const auto file_server = daemon->getFileServer();

    const std::string path{"key"};
    auto              sender     = file_server->popRoot(path, true);
    auto              cmd_result = sync_wait<PopRoot::Result>(executor, std::move(sender), 2s);
    if (const auto* const failure = cetl::get_if<PopRoot::Failure>(&cmd_result))
    {
        spdlog::error("Failed to pop FS root (path='{}', err={}).", path, *failure);
    }
    else
    {
        spdlog::info("File Server responded ok on 'PopRoot'.");
    }
}

/// Demo of daemon's file server - list roots.
///
void tryListRootsScenario(Executor& executor, const Daemon::Ptr& daemon)
{
    using ListRoots = ocvsmd::sdk::FileServer::ListRoots;

    spdlog::info("tryListRootsScenario -----------------");

    const auto file_server = daemon->getFileServer();

    auto sender     = file_server->listRoots();
    auto cmd_result = sync_wait<ListRoots::Result>(executor, std::move(sender), 2s);
    if (const auto* const failure = cetl::get_if<ListRoots::Failure>(&cmd_result))
    {
        spdlog::error("Failed to list FS roots (err={}).", *failure);
    }
    else
    {
        const auto roots = cetl::get<ListRoots::Success>(std::move(cmd_result));
        spdlog::info("File Server responded with list of roots (cnt={}):", roots.size());
        for (std::size_t i = 0; i < roots.size(); ++i)
        {
            spdlog::info("{:4} → '{}'", i, roots[i]);
        }
    }
}

/// Demo of daemon's file server - list, read and write registers of multiple nodes: 42, 43, & 44.
///
void tryListReadWriteRegsOfNodesScenario(Executor&                   executor,
                                         cetl::pmr::memory_resource& memory,
                                         const Daemon::Ptr&          daemon)
{
    using List   = ocvsmd::sdk::NodeRegistryClient::List;
    using Access = ocvsmd::sdk::NodeRegistryClient::Access;

    spdlog::info("tryListReadWriteRegsOfNodesScenario -----------------");

    auto registry = daemon->getNodeRegistryClient();

    // List ALL registers.
    //
    std::array<ocvsmd::sdk::CyphalNodeId, 3> node_ids{42, 43, 44};
    //
    auto sender      = registry->list(node_ids, std::chrono::seconds{1});
    auto list_result = sync_wait<List::Result>(executor, std::move(sender), 2s);
    if (const auto* const list_failure = cetl::get_if<List::Failure>(&list_result))
    {
        spdlog::error("Failed to list registers (err={}).", *list_failure);
    }
    else
    {
        std::set<std::string> reg_names_set;
        const auto            node_id_to_regs = cetl::get<List::Success>(std::move(list_result));
        spdlog::info("Engine responded with list of nodes (cnt={}):", node_id_to_regs.size());
        for (const auto& id_and_regs : node_id_to_regs)
        {
            logRegistryListNodeResult(reg_names_set, id_and_regs.first, id_and_regs.second);
        }
        const std::vector<cetl::string_view>      reg_names_vec{reg_names_set.begin(), reg_names_set.end()};
        const cetl::span<const cetl::string_view> reg_names{reg_names_vec.data(), reg_names_vec.size()};

        // Read ALL registers.
        //
        auto read_sender = registry->read(node_ids, reg_names, std::chrono::seconds{1});
        auto read_result = sync_wait<Access::Result>(executor, std::move(read_sender), 2s);
        if (const auto* const failure = cetl::get_if<Access::Failure>(&read_result))
        {
            spdlog::error("Failed to read registers (err={}).", *failure);
        }
        else
        {
            logRegistryAccessResult(std::move(read_result));
        }

        // Write 'uavcan.node.description' registers.
        //
        const cetl::string_view            reg_key_desc{"uavcan.node.description"};
        std::array<Access::RegKeyValue, 1> reg_keys_and_values{
            Access::RegKeyValue{reg_key_desc, Access::RegValue{&memory}}};
        setRegisterValue(reg_keys_and_values[0].value, "libcyphal demo node3");
        //
        auto write_sender = registry->write(node_ids, reg_keys_and_values, std::chrono::seconds{1});
        auto write_result = sync_wait<Access::Result>(executor, std::move(write_sender), 2s);
        if (const auto* const failure = cetl::get_if<Access::Failure>(&write_result))
        {
            spdlog::error("Failed to write registers (err={}).", *failure);
        }
        else
        {
            logRegistryAccessResult(std::move(write_result));
        }
    }
}

/// Demo of daemon's file server - list, read and write registers of single node 42.
///
void tryListReadWriteRegsOfSingleNodeScenario(Executor&                   executor,
                                              cetl::pmr::memory_resource& memory,
                                              const Daemon::Ptr&          daemon)
{
    using List   = ocvsmd::sdk::NodeRegistryClient::List;
    using Access = ocvsmd::sdk::NodeRegistryClient::Access;

    spdlog::info("tryListReadWriteRegsOfSingleNodeScenario -----------------");

    const auto registry = daemon->getNodeRegistryClient();

    constexpr ocvsmd::sdk::CyphalNodeId node_id{42};
    std::array<cetl::string_view, 1>    reg_keys{"uavcan.node.description"};

    // List
    {
        auto list_node_sender = registry->list(node_id, std::chrono::seconds{1});
        auto list_node_result = sync_wait<List::NodeRegisters::Result>(  //
            executor,
            std::move(list_node_sender),
            2s);

        std::set<std::string> reg_names_set;
        logRegistryListNodeResult(reg_names_set, node_id, list_node_result);
    }

    // Write
    {
        const cetl::string_view            reg_key_desc{"uavcan.node.description"};
        std::array<Access::RegKeyValue, 1> reg_keys_and_values{
            Access::RegKeyValue{reg_key_desc, Access::RegValue{&memory}}};
        setRegisterValue(reg_keys_and_values[0].value, "libcyphal demo node42");

        auto       write_node_sender = registry->write(node_id, reg_keys_and_values, std::chrono::seconds{1});
        const auto write_node_result = sync_wait<Access::NodeRegisters::Result>(  //
            executor,
            std::move(write_node_sender),
            2s);

        logRegistryAccessNodeResult(node_id, write_node_result);
    }

    // Read
    {
        auto       read_node_sender = registry->read(node_id, reg_keys, std::chrono::seconds{1});
        const auto read_node_result = sync_wait<Access::NodeRegisters::Result>(  //
            executor,
            std::move(read_node_sender),
            2s);
        logRegistryAccessNodeResult(node_id, read_node_result);
    }
}

/// Demo of daemon's raw subscriber - subscribes for raw Heartbeat messages, and prints them for some time.
///
void tryRawSubscriberScenario(Executor& executor, cetl::pmr::memory_resource& memory, const Daemon::Ptr& daemon)
{
    using ocvsmd::sdk::RawSubscriber;
    using Heartbeat         = uavcan::node::Heartbeat_1_0;
    using MakeRawSubscriber = Daemon::MakeRawSubscriber;

    spdlog::info("tryMakeRawSubscriberScenario -----------------");

    auto raw_sub_sender = daemon->makeRawSubscriber(Heartbeat::_traits_::FixedPortId, Heartbeat::_traits_::ExtentBytes);
    auto raw_sub_result = sync_wait<MakeRawSubscriber::Result>(executor, std::move(raw_sub_sender), 2s);
    if (const auto* const failure = cetl::get_if<MakeRawSubscriber::Failure>(&raw_sub_result))
    {
        spdlog::error("Failed to make raw subscriber (err={}).", *failure);
        return;
    }
    const auto raw_subscriber = cetl::get<MakeRawSubscriber::Success>(std::move(raw_sub_result));

    constexpr int duration_secs = 10;
    spdlog::info("Printing heartbeat messages for {} secs...", duration_secs);
    const auto until_timepoint = executor.now() + std::chrono::seconds{duration_secs};
    while (until_timepoint > executor.now())
    {
        using Receive = RawSubscriber::Receive;

        auto       raw_msg_sender = raw_subscriber->receive();
        const auto timeout        = until_timepoint - executor.now();
        auto       raw_msg_result = sync_wait<Receive::Result>(executor, std::move(raw_msg_sender), timeout);
        if (const auto* const failure = cetl::get_if<Receive::Failure>(&raw_msg_result))
        {
            spdlog::warn("Failed to receive raw message (err={}).", *failure);
            return;
        }
        const auto raw_msg = cetl::get<Receive::Success>(std::move(raw_msg_result));

        Heartbeat heartbeat{&memory};
        if (!tryDeserializePayload({raw_msg.payload.data.get(), raw_msg.payload.size}, heartbeat))
        {
            spdlog::error("Failed to deserialize heartbeat.");
            return;
        }
        logHeartbeat(heartbeat, raw_msg.publisher_node_id, raw_msg.priority);
    }
}

/// Demo of daemon's raw publisher - publishes `uavcan::time::Synchronization_1_0` messages (every ~1s during 30s).
/// The first half with low priority, and the second half with high priority.
///
void tryPublisherScenario(Executor& executor, cetl::pmr::memory_resource& memory, const Daemon::Ptr& daemon)
{
    using ocvsmd::sdk::RawPublisher;
    using SyncMessage      = uavcan::time::Synchronization_1_0;
    using MakeRawPublisher = Daemon::MakeRawPublisher;

    spdlog::info("tryRawPublisherScenario -----------------");

    auto raw_pub_sender = daemon->makeRawPublisher(SyncMessage::_traits_::FixedPortId);
    auto raw_pub_result = sync_wait<MakeRawPublisher::Result>(executor, std::move(raw_pub_sender), 2s);
    if (const auto* const failure = cetl::get_if<MakeRawPublisher::Failure>(&raw_pub_result))
    {
        spdlog::error("Failed to make raw publisher (err={}).", *failure);
        return;
    }
    const auto raw_publisher = cetl::get<MakeRawPublisher::Success>(std::move(raw_pub_result));

    if (const auto opt_error = raw_publisher->setPriority(ocvsmd::sdk::CyphalPriority::Low))
    {
        spdlog::warn("Failed to set 'low' priority raw publisher (err={}).", *opt_error);
    }

    uavcan::time::Synchronization_1_0 sync_msg{&memory};

    int           counter       = 0;
    constexpr int duration_secs = 30;
    spdlog::info("Publishing time syncs for {} secs...", duration_secs);
    const auto until_timepoint = executor.now() + std::chrono::seconds{duration_secs};
    while (until_timepoint > executor.now())
    {
        ++counter;
        if (counter == 15)
        {
            if (const auto opt_error = raw_publisher->setPriority(ocvsmd::sdk::CyphalPriority::High))
            {
                spdlog::warn("Failed to set 'high' priority raw publisher (err={}).", *opt_error);
            }
        }

        const auto timeout = until_timepoint - executor.now();

        auto sender = raw_publisher->publish(sync_msg, 1s);
        if (const auto error = sync_wait<OptError>(executor, std::move(sender), timeout))
        {
            spdlog::warn("Failed to publish raw message (err={}).", *error);
            return;
        }

        sync_msg.previous_transmission_timestamp_microsecond =
            std::chrono::duration_cast<std::chrono::microseconds>(executor.now().time_since_epoch()).count();

        usleep(1'000'000);  // TODO: Should wait using `sync_wait` (so that the executor keep spinning).
    }
}

}  // namespace

int main(const int argc, const char** const argv)
{
    setupSignalHandlers();
    setupLogging(argc, argv);

    spdlog::info("OCVSMD client started (ver='{}.{}').", VERSION_MAJOR, VERSION_MINOR);
    int result = EXIT_SUCCESS;
    try
    {
        auto&    memory = *cetl::pmr::new_delete_resource();
        Executor executor;

        std::string ipc_connection = "unix-abstract:org.opencyphal.ocvsmd.ipc";
        if (const auto* const env_connection_str = std::getenv("OCVSMD_CONNECTION"))
        {
            ipc_connection = env_connection_str;
        }

        const auto daemon = Daemon::make(memory, executor, ipc_connection);
        if (!daemon)
        {
            spdlog::critical("Failed to create daemon.");
            std::cerr << "Failed to create daemon.";
            return EXIT_FAILURE;
        }

        // Un/Comment needed scenario.
        //
        tryResetNodesScenario(executor, daemon);
        tryBeginSoftwareUpdateScenario(executor, daemon);
        tryPushRootScenario(executor, daemon);
        tryPopRootScenario(executor, daemon);
        tryListRootsScenario(executor, daemon);
        tryListReadWriteRegsOfNodesScenario(executor, memory, daemon);
        tryListReadWriteRegsOfSingleNodeScenario(executor, memory, daemon);
        tryRawSubscriberScenario(executor, memory, daemon);
        tryPublisherScenario(executor, memory, daemon);

        if (g_running == 0)
        {
            spdlog::debug("Received termination signal.");
        }

    } catch (const std::exception& ex)
    {
        spdlog::critical("Unhandled exception: {}", ex.what());
        result = EXIT_FAILURE;
    }
    spdlog::info("OCVSMD client terminated.");

    return result;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
