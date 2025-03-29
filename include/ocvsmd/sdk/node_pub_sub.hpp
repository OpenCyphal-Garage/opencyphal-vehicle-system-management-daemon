//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED
#define OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED

#include "defines.hpp"
#include "execution.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <chrono>
#include <memory>

namespace ocvsmd
{
namespace sdk
{

/// Defines the interface for the Raw Messages Publisher.
///
class RawPublisher
{
public:
    /// Defines a smart pointer type for the interface.
    ///
    /// It's made "shared" b/c execution sender (see `publish` method) implicitly
    /// holds reference to its publisher.
    ///
    using Ptr = std::shared_ptr<RawPublisher>;

    virtual ~RawPublisher() = default;

    // No copy/move semantics.
    RawPublisher(RawPublisher&&)                 = delete;
    RawPublisher(const RawPublisher&)            = delete;
    RawPublisher& operator=(RawPublisher&&)      = delete;
    RawPublisher& operator=(const RawPublisher&) = delete;

    /// Publishes the next raw message using this publisher.
    ///
    /// The client-side (the SDK) will forward the raw data to the corresponding Cyphal network publisher
    /// on the server-side (the daemon). The raw data is forwarded as is, without any interpretation or validation.
    ///
    /// Note, only one operation can be active at a time (per publisher).
    /// In the case of multiple "concurrent" operations, only the last one will report the publishing result.
    /// Any previous still existing operations will be "stalled" and never complete.
    ///
    /// @param raw_msg The raw message data to publish.
    /// @param timeout The maximum time to keep the published raw message as valid in the Cyphal network.
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<OptError>::Ptr publish(OwnMutablePayload&& raw_msg, const std::chrono::microseconds timeout) = 0;

    /// Sets priority for raw messages to be issued by this raw publisher.
    ///
    /// The next and following `publish` operations will use this priority.
    ///
    virtual OptError setPriority(const CyphalPriority priority) = 0;

    /// Publishes the next message using this publisher.
    ///
    /// The client-side (the SDK) will forward the serialized message to the corresponding Cyphal network publisher
    /// on the server-side (the daemon).
    ///
    /// Note, only one operation can be active at a time (per publisher).
    /// In the case of multiple "concurrent" operations, only the last one will report the publishing result.
    /// Any previous still existing operations will be "stalled" and never complete.
    ///
    /// @param message The message to publish.
    /// @param timeout The maximum time to keep the published message as valid in the Cyphal network.
    /// @return An execution sender which emits the async result of the operation.
    ///
    template <typename Message>
    SenderOf<OptError>::Ptr publish(const Message& message, const std::chrono::microseconds timeout)
    {
        return tryPerformOnSerialized(message, [this, timeout](auto payload) {
            //
            return publish(std::move(payload), timeout);
        });
    }

protected:
    RawPublisher() = default;

private:
    template <typename Message, typename Action>
    CETL_NODISCARD static SenderOf<OptError>::Ptr tryPerformOnSerialized(const Message& msg, Action&& action)
    {
#if defined(__cpp_exceptions)
        try
        {
#endif
            // Try to serialize the message to raw payload buffer.
            //
            constexpr std::size_t BufferSize = Message::_traits_::SerializationBufferSizeBytes;
            // NOLINTNEXTLINE(*-avoid-c-arrays)
            OwnMutablePayload payload{BufferSize, std::make_unique<cetl::byte[]>(BufferSize)};
            //
            // No lint b/c of integration with Nunavut.
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            const auto result_size =
                serialize(msg, {reinterpret_cast<std::uint8_t*>(payload.data.get()), payload.size});
            if (result_size)
            {
                payload.size = result_size.value();
                return std::forward<Action>(action)(std::move(payload));
            }
            return just<OptError>(Error{Error::Code::InvalidArgument});

#if defined(__cpp_exceptions)
        } catch (const std::bad_alloc&)
        {
            return just<OptError>(Error{Error::Code::OutOfMemory});
        }
#endif
    }

};  // RawPublisher

/// Defines the interface for the Raw Messages Subscriber.
///
class RawSubscriber
{
public:
    /// Defines a smart pointer type for the interface.
    ///
    /// It's made "shared" b/c execution sender (see `receive` method) implicitly
    /// holds reference to its subscriber.
    ///
    using Ptr = std::shared_ptr<RawSubscriber>;

    virtual ~RawSubscriber() = default;

    // No copy/move semantics.
    RawSubscriber(RawSubscriber&&)                 = delete;
    RawSubscriber(const RawSubscriber&)            = delete;
    RawSubscriber& operator=(RawSubscriber&&)      = delete;
    RawSubscriber& operator=(const RawSubscriber&) = delete;

    /// Defines the result type of the raw subscriber message reception.
    ///
    /// On success, the result is a raw data buffer, its size, and extra metadata.
    /// On failure, the result is an SDK error.
    ///
    struct Receive final
    {
        struct Success
        {
            OwnMutablePayload            payload;
            CyphalPriority               priority;
            cetl::optional<CyphalNodeId> publisher_node_id;
        };
        using Failure = Error;
        using Result  = cetl::variant<Success, Failure>;
    };
    /// Receives the next raw message from this subscriber.
    ///
    /// The server-side (the daemon) will forward the observed raw data on the corresponding Cyphal network subscriber.
    /// The raw data is forwarded as is, without any interpretation or validation.
    ///
    /// Note, only one `receive` operation can be active at a time (per subscriber).
    /// In the case of multiple "concurrent" operations, only the last one will receive the result.
    /// Any previous still existing operations will be "stalled" and never complete.
    /// Also, to not miss any new message, user should immediately initiate
    /// a new `receive` operation after getting the success result of the previous one.
    ///
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<Receive::Result>::Ptr receive() = 0;

protected:
    RawSubscriber() = default;

};  // RawSubscriber

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED
