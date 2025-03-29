//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "services.hpp"

#include "raw_publisher_service.hpp"
#include "raw_subscriber_service.hpp"
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

void registerAllServices(const ScvContext& context)
{
    RawPublisherService::registerWithContext(context);
    RawSubscriberService::registerWithContext(context);
}

}  // namespace relay
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
