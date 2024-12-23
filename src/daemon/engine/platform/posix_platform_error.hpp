//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_PLATFORM_POSIX_PLATFORM_ERROR_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_PLATFORM_POSIX_PLATFORM_ERROR_HPP_INCLUDED

#include <cetl/cetl.hpp>
#include <libcyphal/transport/errors.hpp>

#include <cstdint>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace platform
{

class PosixPlatformError final : public libcyphal::transport::IPlatformError
{
public:
    explicit PosixPlatformError(const int err)
        : code_{err}
    {
        CETL_DEBUG_ASSERT(err > 0, "");
    }
    virtual ~PosixPlatformError()                                = default;
    PosixPlatformError(const PosixPlatformError&)                = default;
    PosixPlatformError(PosixPlatformError&&) noexcept            = default;
    PosixPlatformError& operator=(const PosixPlatformError&)     = default;
    PosixPlatformError& operator=(PosixPlatformError&&) noexcept = default;

    // MARK: IPlatformError

    /// Gets platform-specific error code.
    ///
    /// In this case, the error code is the POSIX error code (aka `errno`).
    ///
    std::uint32_t code() const noexcept override
    {
        return static_cast<std::uint32_t>(code_);
    }

private:
    int code_;

};  // PosixPlatformError

}  // namespace platform
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_PLATFORM_POSIX_PLATFORM_ERROR_HPP_INCLUDED
