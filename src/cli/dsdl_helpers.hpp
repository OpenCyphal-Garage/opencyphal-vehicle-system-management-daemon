//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_CLI_DSDL_HELPERS_HPP_INCLUDED
#define OCVSMD_CLI_DSDL_HELPERS_HPP_INCLUDED

#include <ocvsmd/sdk/defines.hpp>

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <cstdint>

template <typename Message>
CETL_NODISCARD auto tryDeserializePayload(const cetl::span<const cetl::byte> payload, Message& out_message)
{
    // No lint b/c of integration with Nunavut.
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    return deserialize(out_message, {reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()});
}

template <typename Message, typename Action>
CETL_NODISCARD static ocvsmd::sdk::OptError tryPerformOnSerialized(const Message& message, Action&& action)
{
    // Try to serialize the message to raw payload buffer.
    //
    // Next nolint b/c we use a buffer to serialize the message, so no need to zero it (and performance better).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
    std::array<cetl::byte, Message::_traits_::SerializationBufferSizeBytes> buffer;
    //
    // No lint b/c of integration with Nunavut.
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    const auto result_size = serialize(message, {reinterpret_cast<std::uint8_t*>(buffer.data()), buffer.size()});
    if (!result_size)
    {
        return ocvsmd::sdk::OptError{ocvsmd::sdk::Error::Code::InvalidArgument};
    }

    const cetl::span<const cetl::byte> payload{buffer.data(), result_size.value()};
    return std::forward<Action>(action)(payload);
}

#endif  // OCVSMD_CLI_DSDL_HELPERS_HPP_INCLUDED
