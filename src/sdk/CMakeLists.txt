#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

add_cyphal_library(
        NAME sdk
        DSDL_FILES
            "uavcan/node/435.ExecuteCommand.1.3.dsdl"
            "uavcan/register/Value.1.0.dsdl"
        ALLOW_EXPERIMENTAL_LANGUAGES
        LANGUAGE cpp
        LANGUAGE_STANDARD ${CYPHAL_LANGUAGE_STANDARD}
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/dsdl_transpiled
        OUT_LIBRARY_TARGET sdk_transpiled
)

add_library(ocvsmd_sdk
        daemon.cpp
        file_server.cpp
        node_command_client.cpp
        node_registry_client.cpp
        svc/node/access_registers_client.cpp
        svc/node/exec_cmd_client.cpp
        svc/node/list_registers_client.cpp
        svc/file_server/list_roots_client.cpp
        svc/file_server/pop_root_client.cpp
        svc/file_server/push_root_client.cpp
)
target_link_libraries(ocvsmd_sdk
        PUBLIC ${sdk_transpiled}
        PRIVATE ocvsmd_common
)
target_include_directories(ocvsmd_sdk
        PUBLIC ${include_dir}
)
target_include_directories(ocvsmd_sdk SYSTEM
        PUBLIC ${submodules_dir}/cetl/include
        PUBLIC ${submodules_dir}/libcyphal/include
)
