//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_SVC_NODE_EXEC_CMD_SPEC_HPP_INCLUDED
#define OCVSMD_COMMON_SVC_NODE_EXEC_CMD_SPEC_HPP_INCLUDED

#include "ocvsmd/common/svc/node/ExecCmd_0_1.hpp"

namespace ocvsmd
{
namespace common
{
namespace svc
{
namespace node
{

struct ExecCmdSpec
{
    using Request  = ExecCmd::Request_0_1;
    using Response = ExecCmd::Response_0_1;

    constexpr auto static svc_full_name()
    {
        return "ocvsmd.svc.node.exec_cmd";
    }

    ExecCmdSpec() = delete;
};

}  // namespace node
}  // namespace svc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_SVC_NODE_EXEC_CMD_SPEC_HPP_INCLUDED
