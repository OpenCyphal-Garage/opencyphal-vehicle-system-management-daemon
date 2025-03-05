//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_SVC_NODE_LIST_REGISTERS_SERVICE_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_SVC_NODE_LIST_REGISTERS_SERVICE_HPP_INCLUDED

#include "svc/svc_helpers.hpp"

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace svc
{
namespace node
{

/// Defines registration factory of the 'Node: List Registers' service.
///
class ListRegistersService
{
public:
    ListRegistersService() = delete;
    static void registerWithContext(const ScvContext& context);

};  // ListRegistersService

}  // namespace node
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_SVC_NODE_LIST_REGISTERS_SERVICE_HPP_INCLUDED
