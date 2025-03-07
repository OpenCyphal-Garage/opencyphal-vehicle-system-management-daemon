//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "services.hpp"

#include "access_registers_service.hpp"
#include "exec_cmd_service.hpp"
#include "list_registers_service.hpp"
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

void registerAllServices(const ScvContext& context)
{
    ExecCmdService::registerWithContext(context);
    ListRegistersService::registerWithContext(context);
    AccessRegistersService::registerWithContext(context);
}

}  // namespace node
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
