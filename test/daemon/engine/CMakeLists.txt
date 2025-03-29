#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

add_executable(engine_tests
        main.cpp
        svc/node/test_exec_cmd_service.cpp
        svc/relay/test_raw_publisher_service.cpp
        svc/relay/test_raw_subscriber_service.cpp
)
target_link_libraries(engine_tests
        ocvsmd_engine
        GTest::gmock
)
target_include_directories(engine_tests
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

gtest_discover_tests(engine_tests)
