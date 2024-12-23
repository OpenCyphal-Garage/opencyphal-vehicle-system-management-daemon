#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.27)

project(ocvsmd
        LANGUAGES C CXX
        HOMEPAGE_URL https://github.com/OpenCyphal-Garage/opencyphal-vehicle-system-management-daemon
)

enable_testing()

set(NO_STATIC_ANALYSIS OFF CACHE BOOL "disable static analysis")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the output binary directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(src_dir "${CMAKE_SOURCE_DIR}/src")
set(test_dir "${CMAKE_SOURCE_DIR}/test")
set(include_dir "${CMAKE_SOURCE_DIR}/include")
set(submodules_dir "${CMAKE_SOURCE_DIR}/submodules")

# clang-format
find_program(clang_format NAMES clang-format)
if (NOT clang_format)
    message(STATUS "Could not locate clang-format")
else ()
    file(GLOB_RECURSE format_files
            ${include_dir}/**/*.hpp
            ${src_dir}/**/*.[ch]
            ${src_dir}/**/*.[ch]pp
            ${test_dir}/**/*.[ch]pp
    )
    message(STATUS "Using clang-format: ${clang_format}")
    add_custom_target(format COMMAND ${clang_format} -i -fallback-style=none -style=file --verbose ${format_files})
endif ()

# Use -DNO_STATIC_ANALYSIS=1 to suppress static analysis.
# If not suppressed, the tools used here shall be available, otherwise the build will fail.
if (NOT NO_STATIC_ANALYSIS)
    # clang-tidy (separate config files per directory)
    find_program(clang_tidy NAMES clang-tidy)
    if (NOT clang_tidy)
        message(FATAL_ERROR "Could not locate clang-tidy")
    endif ()
    message(STATUS "Using clang-tidy: ${clang_tidy}")
    set(CMAKE_CXX_CLANG_TIDY ${clang_tidy})
endif ()

# Forward the revision information to the compiler so that we could expose it at runtime. This is entirely optional.
execute_process(
        COMMAND git rev-parse --short=16 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE vcs_revision_id
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "vcs_revision_id: ${vcs_revision_id}")
add_definitions(
        -DVERSION_MAJOR=0
        -DVERSION_MINOR=1
        -DVCS_REVISION_ID=0x${vcs_revision_id}ULL
        -DNODE_NAME="org.opencyphal.ocvsmd"
)
if (DEFINED PLATFORM_LINUX_TYPE)
    if(${PLATFORM_LINUX_TYPE} STREQUAL "bsd")
        add_definitions(-DPLATFORM_LINUX_TYPE_BSD)
    elseif(${PLATFORM_LINUX_TYPE} STREQUAL "debian")
        add_definitions(-DPLATFORM_LINUX_TYPE_DEBIAN)
    endif()
endif()

add_subdirectory(src)
add_subdirectory(test)
