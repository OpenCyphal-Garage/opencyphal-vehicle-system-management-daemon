#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.27)

add_library(ocvsmd_common
        ipc/unix_socket_client.cpp
        ipc/unix_socket_server.cpp
)
target_include_directories(ocvsmd_common
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(ocvsmd_common SYSTEM
        PUBLIC ${submodules_dir}/cetl/include
        PUBLIC ${submodules_dir}/libcyphal/include
)
