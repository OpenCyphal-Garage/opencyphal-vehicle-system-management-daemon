#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.27)

add_library(ocvsmd_sdk
        daemon.cpp
)
target_link_libraries(ocvsmd_sdk
        PRIVATE ocvsmd_common
)
target_include_directories(ocvsmd_sdk
        PUBLIC ${include_dir}
)
