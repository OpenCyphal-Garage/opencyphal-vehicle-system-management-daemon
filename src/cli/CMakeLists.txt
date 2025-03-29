#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

add_cyphal_library(
        NAME cli
        DSDL_FILES
        "uavcan/node/7509.Heartbeat.1.0.dsdl"
        "uavcan/time/7168.Synchronization.1.0.dsdl"
        ALLOW_EXPERIMENTAL_LANGUAGES
        LANGUAGE cpp
        LANGUAGE_STANDARD ${CYPHAL_LANGUAGE_STANDARD}
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/dsdl_transpiled
        OUT_LIBRARY_TARGET cli_transpiled
)

add_executable(ocvsmd-cli
        main.cpp
)
target_link_libraries(ocvsmd-cli
        PUBLIC ${cli_transpiled}
        PUBLIC ocvsmd_sdk
)
target_include_directories(ocvsmd-cli SYSTEM
        PUBLIC ${submodules_dir}/spdlog/include
)
add_dependencies(ocvsmd-cli
        ocvsmd
)
