//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_SVC_RELAY_RAW_PUBLISHER_SERVICE_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_SVC_RELAY_RAW_PUBLISHER_SERVICE_HPP_INCLUDED

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

/// Defines registration factory of the 'Relay: Raw Publisher' service.
///
class RawPublisherService
{
public:
    RawPublisherService() = delete;
    static void registerWithContext(const ScvContext& context);

};  // RawPublisherService

}  // namespace relay
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_SVC_RELAY_RAW_PUBLISHER_SERVICE_HPP_INCLUDED
