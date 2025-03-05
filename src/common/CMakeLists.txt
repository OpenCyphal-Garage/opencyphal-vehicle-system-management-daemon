#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

set(dsdl_ocvsmd_dir ${CMAKE_CURRENT_SOURCE_DIR}/dsdl/ocvsmd)
set(dsdl_ocvsmd_files
        ${dsdl_ocvsmd_dir}/common/ipc/Route.0.1.dsdl
        ${dsdl_ocvsmd_dir}/common/ipc/RouteChannelEnd.0.1.dsdl
        ${dsdl_ocvsmd_dir}/common/ipc/RouteChannelMsg.0.1.dsdl
        ${dsdl_ocvsmd_dir}/common/ipc/RouteConnect.0.1.dsdl
        ${dsdl_ocvsmd_dir}/common/svc/file_server/ListRoots.0.1.dsdl
        ${dsdl_ocvsmd_dir}/common/svc/file_server/PopRoot.0.1.dsdl
        ${dsdl_ocvsmd_dir}/common/svc/file_server/PushRoot.0.1.dsdl
        ${dsdl_ocvsmd_dir}/common/svc/node/ExecCmd.0.1.dsdl
        ${dsdl_ocvsmd_dir}/common/svc/node/ListRegisters.0.1.dsdl
        ${dsdl_ocvsmd_dir}/common/svc/node/UavcanNodeExecCmdReq.0.1.dsdl
        ${dsdl_ocvsmd_dir}/common/svc/node/UavcanNodeExecCmdRes.0.1.dsdl
)

add_cyphal_library(
        NAME common
        DSDL_FILES ${dsdl_ocvsmd_files}
        DSDL_NAMESPACES ${dsdl_ocvsmd_dir}
        ALLOW_EXPERIMENTAL_LANGUAGES
        LANGUAGE cpp
        LANGUAGE_STANDARD ${CYPHAL_LANGUAGE_STANDARD}
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/dsdl_transpiled
        OUT_LIBRARY_TARGET common_transpiled
)

add_library(ocvsmd_common
        io/io.cpp
        io/socket_address.cpp
        ipc/client_router.cpp
        ipc/pipe/socket_base.cpp
        ipc/pipe/socket_client.cpp
        ipc/pipe/socket_server.cpp
        ipc/server_router.cpp
)
target_link_libraries(ocvsmd_common
        PUBLIC ${common_transpiled}
)
target_include_directories(ocvsmd_common
        PUBLIC ${include_dir}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(ocvsmd_common SYSTEM
        PUBLIC ${submodules_dir}/cetl/include
        PUBLIC ${submodules_dir}/libcyphal/include
        PUBLIC ${submodules_dir}/spdlog/include
)
add_dependencies(ocvsmd_common
        ${common_transpiled}
)
