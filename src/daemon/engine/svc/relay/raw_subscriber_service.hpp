//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_SVC_RELAY_RAW_SUBSCRIBER_SERVICE_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_SVC_RELAY_RAW_SUBSCRIBER_SERVICE_HPP_INCLUDED

#include "svc/svc_helpers.hpp"

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace svc
{
namespace relay
{

/// Defines registration factory of the 'Relay: Raw Subscriber' service.
///
class RawSubscriberService
{
public:
    RawSubscriberService() = delete;
    static void registerWithContext(const ScvContext& context);

};  // RawSubscriberService

}  // namespace relay
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_SVC_RELAY_RAW_SUBSCRIBER_SERVICE_HPP_INCLUDED
