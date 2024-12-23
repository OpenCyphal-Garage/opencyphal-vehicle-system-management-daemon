//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_PLATFORM_DEFINES_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_PLATFORM_DEFINES_HPP_INCLUDED

#ifdef PLATFORM_LINUX_TYPE_BSD
#    include "bsd/kqueue_single_threaded_executor.hpp"
#else
#    include "debian/epoll_single_threaded_executor.hpp"
#endif

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace platform
{

#ifdef PLATFORM_LINUX_TYPE_BSD
using SingleThreadedExecutor = bsd::KqueueSingleThreadedExecutor;
#else
using SingleThreadedExecutor = debian::EpollSingleThreadedExecutor;
#endif

}  // namespace platform
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_PLATFORM_DEFINES_HPP_INCLUDED
