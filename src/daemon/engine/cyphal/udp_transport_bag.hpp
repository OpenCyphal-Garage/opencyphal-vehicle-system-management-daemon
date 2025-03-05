//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_CYPHAL_UDP_TRANSPORT_BAG_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_CYPHAL_UDP_TRANSPORT_BAG_HPP_INCLUDED

#include "any_transport_bag.hpp"
#include "config.hpp"
#include "logging.hpp"
#include "platform/udp/udp_media.hpp"
#include "transport_helpers.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/transport/errors.hpp>
#include <libcyphal/transport/udp/udp_transport.hpp>
#include <libcyphal/transport/udp/udp_transport_impl.hpp>
#include <libcyphal/types.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace cyphal
{

/// Holds (internally) instance of the UDP transport and its media (if any).
///
class UdpTransportBag final : public AnyTransportBag
{
    /// Defines private specification for making interface unique ptr.
    ///
    struct Spec
    {
        explicit Spec() = default;
    };

public:
    Transport& getTransport() const override
    {
        CETL_DEBUG_ASSERT(transport_, "");
        return *transport_;
    }

    static Ptr make(cetl::pmr::memory_resource& memory, libcyphal::IExecutor& executor, const Config::Ptr& config)
    {
        CETL_DEBUG_ASSERT(config, "");

        static const std::string udp_prefix{"udp://"};

        std::string udp_ifaces;
        for (const auto& iface : config->getCyphalTransportInterfaces())
        {
            if (0 == iface.compare(0, udp_prefix.size(), udp_prefix))
            {
                udp_ifaces += iface.substr(udp_prefix.size());
                udp_ifaces += ",";
            }
        }
        common::getLogger("io")->trace("Attempting to create UDP transport (ifaces=[{}])…", udp_ifaces);

        auto transport_bag = std::make_unique<UdpTransportBag>(Spec{}, memory, executor);

        auto& media_collection = transport_bag->media_collection_;
        media_collection.parse(udp_ifaces);
        if (media_collection.count() == 0)
        {
            return nullptr;
        }

        auto maybe_transport = makeTransport({memory}, executor, media_collection.span(), TxQueueCapacity);
        if (const auto* failure = cetl::get_if<libcyphal::transport::FactoryFailure>(&maybe_transport))
        {
            (void) failure;
            common::getLogger("io")->warn("Failed to create UDP transport.");
            return nullptr;
        }
        transport_bag->transport_ = cetl::get<TransportPtr>(std::move(maybe_transport));

        // To support redundancy (multiple homogeneous interfaces), it's important to have a non-default
        // handler which "swallows" expected transient failures (by returning `nullopt` result).
        // Otherwise, the default Cyphal behavior will fail/interrupt current and future transfers
        // if some of its media encounter transient failures - thus breaking the whole redundancy goal,
        // namely, maintain communication if at least one of the interfaces is still up and running.
        //
        transport_bag->transport_->setTransientErrorHandler([](auto&) { return cetl::nullopt; });
        // transport_bag->transport_->setTransientErrorHandler(TransportHelpers::UdpTransientErrorReporter{});

        common::getLogger("io")->debug("Created UDP transport (ifaces={})", media_collection.count());
        return transport_bag;
    }

    UdpTransportBag(Spec, cetl::pmr::memory_resource& memory, libcyphal::IExecutor& executor)
        : memory_{memory}
        , executor_{executor}
        , media_collection_{memory, executor, memory}
    {
    }

private:
    using TransportPtr = libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport>;

    static constexpr std::size_t TxQueueCapacity = 16;

    cetl::pmr::memory_resource&       memory_;
    libcyphal::IExecutor&             executor_;
    platform::udp::UdpMediaCollection media_collection_;
    TransportPtr                      transport_;

};  // UdpTransportBag

}  // namespace cyphal
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_CYPHAL_UDP_TRANSPORT_BAG_HPP_INCLUDED
