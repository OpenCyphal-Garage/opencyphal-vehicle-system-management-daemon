#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.22.0)

add_executable(ocvsmd-cli main.cpp)
target_link_libraries(ocvsmd-cli ocvsmd-common)
