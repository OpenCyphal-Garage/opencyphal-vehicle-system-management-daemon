#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.22.0)

capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS calculator.capnp)

message(STATUS "CAPNP_SRCS: ${CAPNP_SRCS}")
message(STATUS "CAPNP_HDRS: ${CAPNP_HDRS}")

add_library(
        ocvsmd-common
        ${CAPNP_SRCS}
)

target_link_libraries(ocvsmd-common ${CAPNP_LIBRARIES})
target_include_directories(ocvsmd-common PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
set_source_files_properties(${CAPNP_SRCS} PROPERTIES SKIP_LINTING ON)
