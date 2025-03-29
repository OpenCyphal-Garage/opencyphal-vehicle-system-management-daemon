//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_SVC_RELAY_RAW_SUBSCRIBER_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_SVC_RELAY_RAW_SUBSCRIBER_CLIENT_HPP_INCLUDED

#include "ipc/client_router.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "ocvsmd/sdk/node_pub_sub.hpp"
#include "svc/relay/raw_subscriber_spec.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <memory>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace relay
{

/// Defines interface of the 'Relay: Raw Subscriber' service client.
///
class RawSubscriberClient
{
public:
    using Ptr  = std::shared_ptr<RawSubscriberClient>;
    using Spec = common::svc::relay::RawSubscriberSpec;

    using Success = RawSubscriber::Ptr;
    using Failure = Error;
    using Result  = cetl::variant<Success, Failure>;

    CETL_NODISCARD static Ptr make(cetl::pmr::memory_resource&           memory,
                                   const common::ipc::ClientRouter::Ptr& ipc_router,
                                   const Spec::Request&                  request);

    RawSubscriberClient(RawSubscriberClient&&)                 = delete;
    RawSubscriberClient(const RawSubscriberClient&)            = delete;
    RawSubscriberClient& operator=(RawSubscriberClient&&)      = delete;
    RawSubscriberClient& operator=(const RawSubscriberClient&) = delete;

    virtual ~RawSubscriberClient() = default;

    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
        submitImpl([receive = std::forward<Receiver>(receiver)](Result&& result) mutable {
            //
            receive(std::move(result));
        });
    }

protected:
    RawSubscriberClient() = default;

    virtual void submitImpl(std::function<void(Result&&)>&& receiver) = 0;

};  // RawSubscriberClient

}  // namespace relay
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_RELAY_RAW_SUBSCRIBER_CLIENT_HPP_INCLUDED
