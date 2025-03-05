//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_SVC_NODE_LIST_REGISTERS_SPEC_HPP_INCLUDED
#define OCVSMD_COMMON_SVC_NODE_LIST_REGISTERS_SPEC_HPP_INCLUDED

#include "ocvsmd/common/svc/node/ListRegisters_0_1.hpp"

namespace ocvsmd
{
namespace common
{
namespace svc
{
namespace node
{

struct ListRegistersSpec
{
    using Request  = ListRegisters::Request_0_1;
    using Response = ListRegisters::Response_0_1;

    constexpr auto static svc_full_name()
    {
        return "ocvsmd.svc.node.list_registers";
    }

    ListRegistersSpec() = delete;
};

}  // namespace node
}  // namespace svc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_SVC_NODE_LIST_REGISTERS_SPEC_HPP_INCLUDED
