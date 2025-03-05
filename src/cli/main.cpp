//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "setup_logging.hpp"

#include <ocvsmd/platform/defines.hpp>
#include <ocvsmd/sdk/daemon.hpp>
#include <ocvsmd/sdk/execution.hpp>
#include <ocvsmd/sdk/node_command_client.hpp>

#include <cetl/pf17/cetlpf.hpp>

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <signal.h>  // NOLINT
#include <utility>
#include <vector>

namespace
{

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

}  // namespace

int main(const int argc, const char** const argv)
{
    using std::chrono_literals::operator""s;
    using Executor = ocvsmd::platform::SingleThreadedExecutor;

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

        const auto daemon = ocvsmd::sdk::Daemon::make(memory, executor, ipc_connection);
        if (!daemon)
        {
            spdlog::critical("Failed to create daemon.");
            std::cerr << "Failed to create daemon.";
            return EXIT_FAILURE;
        }

#if 0  // NOLINT

        // Demo of daemon's node command client - sending a command to node 42, 43 & 44.
        {
            using Command = ocvsmd::sdk::NodeCommandClient::Command;

            auto node_cmd_client = daemon->getNodeCommandClient();

            const std::vector<std::uint16_t> node_ids = {42, 43, 44};
            // auto sender     = node_cmd_client->restart({node_ids.data(), node_ids.size()});
            auto sender     = node_cmd_client->beginSoftwareUpdate({node_ids.data(), node_ids.size()}, "firmware.bin");
            auto cmd_result = ocvsmd::sdk::sync_wait<Command::Result>(executor, std::move(sender));
            if (const auto* const err = cetl::get_if<Command::Failure>(&cmd_result))
            {
                spdlog::error("Failed to send command: {}", std::strerror(*err));
            }
            else
            {
                const auto responds = cetl::get<Command::Success>(std::move(cmd_result));
                for (const auto& node_and_respond : responds)
                {
                    spdlog::info("Node {} responded with status: {}.",
                                 node_and_respond.first,
                                 node_and_respond.second.status);
                }
            }
        }
#endif
#if 0  // NOLINT

        // Demo of daemon's file server - push root.
        {
            using PushRoot = ocvsmd::sdk::FileServer::PushRoot;

            auto file_server = daemon->getFileServer();

            const std::string path{"key"};
            auto              sender     = file_server->pushRoot(path, true);
            auto              cmd_result = ocvsmd::sdk::sync_wait<PushRoot::Result>(executor, std::move(sender));
            if (const auto* const err = cetl::get_if<PushRoot::Failure>(&cmd_result))
            {
                spdlog::error("Failed to push FS root (path='{}'): {}.", path, std::strerror(*err));
            }
            else
            {
                spdlog::info("File Server responded ok on 'PushRoot'.");
            }
        }
#endif
#if 0  // NOLINT

        // Demo of daemon's file server - pop root.
        {
            using PopRoot = ocvsmd::sdk::FileServer::PopRoot;

            auto file_server = daemon->getFileServer();

            const std::string path{"key"};
            auto              sender     = file_server->popRoot(path, true);
            auto              cmd_result = ocvsmd::sdk::sync_wait<PopRoot::Result>(executor, std::move(sender));
            if (const auto* const err = cetl::get_if<PopRoot::Failure>(&cmd_result))
            {
                spdlog::error("Failed to pop FS root (path='{}'): {}.", path, std::strerror(*err));
            }
            else
            {
                spdlog::info("File Server responded ok on 'PopRoot'.");
            }
        }
#endif
#if 0  // NOLINT

        // Demo of daemon's file server - getting the list of roots.
        {
            using ListRoots = ocvsmd::sdk::FileServer::ListRoots;

            auto file_server = daemon->getFileServer();

            auto sender     = file_server->listRoots();
            auto cmd_result = ocvsmd::sdk::sync_wait<ListRoots::Result>(executor, std::move(sender));
            if (const auto* const err = cetl::get_if<ListRoots::Failure>(&cmd_result))
            {
                spdlog::error("Failed to list FS roots: {}", std::strerror(*err));
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
#endif
#if 1  // NOLINT

        // Demo of daemon's registry client - getting the list of registers from nodes.
        {
            using List = ocvsmd::sdk::NodeRegistryClient::List;

            auto registry = daemon->getNodeRegistryClient();

            const std::vector<std::uint16_t> node_ids = {123, 42, 43, 44};
            auto sender      = registry->list({node_ids.data(), node_ids.size()}, std::chrono::seconds{1});
            auto list_result = ocvsmd::sdk::sync_wait<List::Result>(executor, std::move(sender));
            if (const auto* const err = cetl::get_if<List::Failure>(&list_result))
            {
                spdlog::error("Failed to list registers: {}", std::strerror(*err));
            }
            else
            {
                const auto node_id_to_regs = cetl::get<List::Success>(std::move(list_result));
                spdlog::info("Engine responded with list of nodes (cnt={}):", node_id_to_regs.size());
                for (const auto& id_and_regs : node_id_to_regs)
                {
                    using NodeRegs = List::NodeRegisters;

                    if (const auto* const node_err = cetl::get_if<NodeRegs::Failure>(&id_and_regs.second))
                    {
                        spdlog::warn("{:4} → err={}", id_and_regs.first, *node_err);
                        continue;
                    }
                    const auto& node_regs = cetl::get<NodeRegs::Success>(id_and_regs.second);
                    for (const auto& reg_name : node_regs)
                    {
                        spdlog::info("{:4} → '{}'", id_and_regs.first, reg_name);
                    }
                }
            }
        }
#endif

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
