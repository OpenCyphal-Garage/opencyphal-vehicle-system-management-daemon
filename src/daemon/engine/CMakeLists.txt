#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

# Define type generation and header library all in one go.
#
set(dsdl_types_in_engine # List all the DSDL types used in the engine
        uavcan/file/405.GetInfo.0.2.dsdl
        uavcan/file/406.List.0.2.dsdl
        uavcan/file/407.Modify.1.1.dsdl
        uavcan/file/408.Read.1.1.dsdl
        uavcan/file/409.Write.1.1.dsdl
        uavcan/node/430.GetInfo.1.0.dsdl
        uavcan/node/435.ExecuteCommand.1.3.dsdl
        uavcan/node/7509.Heartbeat.1.0.dsdl
        uavcan/register/384.Access.1.0.dsdl
        uavcan/register/385.List.1.0.dsdl
)
add_cyphal_library(
        NAME engine
        DSDL_FILES ${dsdl_types_in_engine}
        ALLOW_EXPERIMENTAL_LANGUAGES
        LANGUAGE cpp
        LANGUAGE_STANDARD ${CYPHAL_LANGUAGE_STANDARD}
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/dsdl_transpiled
        OUT_LIBRARY_TARGET engine_transpiled
)

add_library(udpard
        ${submodules_dir}/libudpard/libudpard/udpard.c
)
target_include_directories(udpard
        INTERFACE SYSTEM ${submodules_dir}/libudpard/libudpard
)

add_library(canard
        ${submodules_dir}/libcanard/libcanard/canard.c
)
target_include_directories(canard
        INTERFACE SYSTEM ${submodules_dir}/libcanard/libcanard
)

add_library(ocvsmd_engine
        config.cpp
        cyphal/file_provider.cpp
        engine.cpp
        platform/udp/udp.c
        svc/file_server/list_roots_service.cpp
        svc/file_server/pop_root_service.cpp
        svc/file_server/push_root_service.cpp
        svc/file_server/services.cpp
        svc/node/access_registers_service.cpp
        svc/node/exec_cmd_service.cpp
        svc/node/list_registers_service.cpp
        svc/node/services.cpp
)
target_link_libraries(ocvsmd_engine
        PUBLIC udpard
        PUBLIC canard
        PUBLIC ${engine_transpiled}
        PUBLIC ocvsmd_common
)
if (${PLATFORM_OS_TYPE} STREQUAL "linux")
    target_sources(ocvsmd_engine
            PRIVATE platform/can/socketcan.c
    )
endif ()
target_include_directories(ocvsmd_engine
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(ocvsmd_engine SYSTEM
        PUBLIC ${submodules_dir}/cetl/include
        PUBLIC ${submodules_dir}/libcyphal/include
        PRIVATE ${submodules_dir}/toml11/include
)
add_dependencies(ocvsmd_engine
        ${engine_transpiled}
)
