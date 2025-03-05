//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_FACTORY_HPP_INCLUDED
#define OCVSMD_SDK_FACTORY_HPP_INCLUDED

#include <ocvsmd/sdk/file_server.hpp>
#include <ocvsmd/sdk/node_command_client.hpp>
#include <ocvsmd/sdk/node_registry_client.hpp>

#include "ipc/client_router.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

namespace ocvsmd
{
namespace sdk
{

struct Factory
{
    CETL_NODISCARD static FileServer::Ptr makeFileServer(cetl::pmr::memory_resource&    memory,
                                                         common::ipc::ClientRouter::Ptr ipc_router);

    CETL_NODISCARD static NodeCommandClient::Ptr makeNodeCommandClient(cetl::pmr::memory_resource&    memory,
                                                                       common::ipc::ClientRouter::Ptr ipc_router);

    CETL_NODISCARD static NodeRegistryClient::Ptr makeNodeRegistryClient(cetl::pmr::memory_resource&    memory,
                                                                         common::ipc::ClientRouter::Ptr ipc_router);

};  // Factory

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_FACTORY_HPP_INCLUDED
