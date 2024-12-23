#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.27)

add_subdirectory(engine)

add_executable(ocvsmd
        main.cpp
)
target_link_libraries(ocvsmd
        PRIVATE ocvsmd_engine
)
