//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_SVC_RELAY_RAW_SUBSCRIBER_SPEC_HPP_INCLUDED
#define OCVSMD_COMMON_SVC_RELAY_RAW_SUBSCRIBER_SPEC_HPP_INCLUDED

#include "ocvsmd/common/svc/relay/RawSubscriber_0_1.hpp"

namespace ocvsmd
{
namespace common
{
namespace svc
{
namespace relay
{

/// Defines IPC internal housekeeping specification for the `RawSubscriber` service.
///
struct RawSubscriberSpec
{
    using Request  = RawSubscriber::Request_0_1;
    using Response = RawSubscriber::Response_0_1;

    constexpr auto static svc_full_name()
    {
        return "ocvsmd.svc.relay.raw_subscriber";
    }

    RawSubscriberSpec() = delete;
};

}  // namespace relay
}  // namespace svc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_SVC_RELAY_RAW_SUBSCRIBER_SPEC_HPP_INCLUDED
