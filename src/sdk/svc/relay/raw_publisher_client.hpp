//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_SVC_RELAY_RAW_PUBLISHER_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_SVC_RELAY_RAW_PUBLISHER_CLIENT_HPP_INCLUDED

#include "ipc/client_router.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "ocvsmd/sdk/node_pub_sub.hpp"
#include "svc/relay/raw_publisher_spec.hpp"

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

/// Defines interface of the 'Relay: Raw Publisher' service client.
///
class RawPublisherClient
{
public:
    using Ptr  = std::shared_ptr<RawPublisherClient>;
    using Spec = common::svc::relay::RawPublisherSpec;

    using Success = RawPublisher::Ptr;
    using Failure = Error;
    using Result  = cetl::variant<Success, Failure>;

    CETL_NODISCARD static Ptr make(cetl::pmr::memory_resource&           memory,
                                   const common::ipc::ClientRouter::Ptr& ipc_router,
                                   const Spec::Request&                  request);

    RawPublisherClient(RawPublisherClient&&)                 = delete;
    RawPublisherClient(const RawPublisherClient&)            = delete;
    RawPublisherClient& operator=(RawPublisherClient&&)      = delete;
    RawPublisherClient& operator=(const RawPublisherClient&) = delete;

    virtual ~RawPublisherClient() = default;

    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
        submitImpl([receive = std::forward<Receiver>(receiver)](Result&& result) mutable {
            //
            receive(std::move(result));
        });
    }

protected:
    RawPublisherClient() = default;

    virtual void submitImpl(std::function<void(Result&&)>&& receiver) = 0;

};  // RawPublisherClient

}  // namespace relay
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_RELAY_RAW_PUBLISHER_CLIENT_HPP_INCLUDED
