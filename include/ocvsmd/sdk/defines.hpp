//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_DEFINES_HPP_INCLUDED
#define OCVSMD_SDK_DEFINES_HPP_INCLUDED

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>

namespace ocvsmd
{
namespace sdk
{

struct Error
{
    /// Defines platform-independent error codes.
    ///
    /// Note: Please keep raw values of the error codes as they are defined.
    ///       This is b/c the raw error codes are passed via "wire" (in the DSDL types).
    ///
    enum class Code : std::uint32_t  // NOLINT(*-enum-size)
    {
        Other = 1,
        Busy,
        NoEntry,
        Canceled,
        TimedOut,
        Shutdown,
        OutOfMemory,
        Disconnected,
        NotConnected,
        AlreadyExists,
        InvalidArgument,
        OperationInProgress,

    };  // Code

    explicit Error(const Code code) noexcept
        : code_{code}
    {
    }

    Error(const Code code, const std::int32_t errno_) noexcept
        : code_{code}
        , opt_errno_{errno_}
    {
    }

    /// Gets platform-independent error code.
    ///
    Code getCode() const noexcept
    {
        return code_;
    }

    /// Gets original/source `errno` value if available.
    ///
    /// The result value is platform-dependent, and so should not be used for decision-making.
    /// Provided mostly for logging and debugging purposes.
    ///
    cetl::optional<std::int32_t> getOptErrno() const noexcept
    {
        return opt_errno_;
    }

private:
    friend bool operator==(const Error& lhs, const Error& rhs) noexcept;

    Code                         code_;
    cetl::optional<std::int32_t> opt_errno_;

};  // Error

/// Compares two errors by their corresponding codes.
///
/// Note that `opt_errno_` fields are intentionally not considered in the comparison (see `getOptErrno()` notes).
///
inline bool operator==(const Error& lhs, const Error& rhs) noexcept
{
    return lhs.getCode() == rhs.getCode();
}

/// Defines an optional SDK error. `nullopt` means no error (aka success).
///
using OptError = cetl::optional<Error>;

/// Defines the type of the Cyphal node ID.
///
using CyphalNodeId = std::uint16_t;

/// Defines span of the Cyphal node IDs.
///
using CyphalNodeIds = cetl::span<CyphalNodeId>;

/// Defines the type of the Cyphal port ID.
///
using CyphalPortId = std::uint16_t;

/// Defines priorities for Cyphal network messages.
///
/// Note, raw values are exactly the same as defined in the Cyphal specification -
/// this is done to simplify the conversion between the SDK and the Cyphal network.
///
enum class CyphalPriority : std::uint8_t
{
    Exceptional = 0,
    Immediate,
    Fast,
    High,
    Nominal,
    Low,
    Slow,
    Optional,

};  // CyphalPriority

/// Defines helper which owns mutable raw data buffer.
///
struct OwnMutablePayload
{
    /// Holds the size of the raw data buffer. It could be less than it was allocated.
    std::size_t size;

    /// Holds smart pointer to the raw data buffer.
    std::unique_ptr<cetl::byte[]> data;  // NOLINT(*-avoid-c-arrays)

};  // OwnMutablePayload

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_DEFINES_HPP_INCLUDED
