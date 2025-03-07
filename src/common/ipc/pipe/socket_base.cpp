//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "socket_base.hpp"

#include "ipc/ipc_types.hpp"
#include "ocvsmd/platform/posix_utils.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <numeric>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace pipe
{
namespace
{

constexpr std::uint32_t MsgHeaderSignature = 0x5356434F;     // 'OCVS'
constexpr std::size_t   MsgPayloadMaxSize  = 1ULL << 20ULL;  // 1 MB

}  // namespace

int SocketBase::send(const IoState& io_state, const Payloads payloads) const
{
    // 1. Write the message header (signature and total size of the following fragments).
    //
    const std::size_t total_payload_size = std::accumulate(  // NOLINT
        payloads.begin(),
        payloads.end(),
        0ULL,
        [](const std::size_t acc, const Payload payload) {
            //
            return acc + payload.size();
        });
    if (const int err = platform::posixSyscallError([total_payload_size, &io_state] {
            //
            const IoState::MsgHeader msg_header{MsgHeaderSignature, static_cast<std::uint32_t>(total_payload_size)};
            return ::send(io_state.fd.get(), &msg_header, sizeof(msg_header), MSG_DONTWAIT);
        }))
    {
        logger_->error("SocketBase: Failed to send msg header (fd={}): {}.", io_state.fd.get(), std::strerror(err));
        return err;
    }

    // 2. Write the message payload fragments.
    //
    for (const auto payload : payloads)
    {
        if (const int err = platform::posixSyscallError([payload, &io_state] {
                //
                return ::send(io_state.fd.get(), payload.data(), payload.size(), MSG_DONTWAIT);
            }))
        {
            logger_->error("SocketBase: Failed to send msg payload (fd={}): {}.",
                           io_state.fd.get(),
                           std::strerror(err));
            return err;
        }
    }
    return 0;
}

int SocketBase::receiveData(IoState& io_state) const
{
    // 1. Receive and validate the message header.
    //
    if (auto* const msg_header_ptr = cetl::get_if<IoState::MsgHeader>(&io_state.rx_msg_part))
    {
        auto& msg_header = *msg_header_ptr;

        CETL_DEBUG_ASSERT(io_state.rx_partial_size < sizeof(msg_header), "");
        if (io_state.rx_partial_size < sizeof(msg_header))
        {
            // Try to read the remaining part of the message header.
            //
            ssize_t bytes_read = 0;
            if (const auto err = platform::posixSyscallError([&io_state, &bytes_read, &msg_header] {
                    //
                    // No lint b/c of low-level (potentially partial) reading.
                    // NOLINTNEXTLINE(*-reinterpret-cast, *-pointer-arithmetic)
                    auto* const dst_buf = reinterpret_cast<std::uint8_t*>(&msg_header) + io_state.rx_partial_size;
                    //
                    const auto bytes_to_read = sizeof(msg_header) - io_state.rx_partial_size;
                    return bytes_read        = ::recv(io_state.fd.get(), dst_buf, bytes_to_read, MSG_DONTWAIT);
                }))
            {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                {
                    // No data available yet - that's ok, the next attempt will try to read again.
                    //
                    logger_->trace("Msg header read would block (fd={}).", io_state.fd.get());
                    return 0;
                }
                if (errno == ECONNRESET)
                {
                    logger_->debug("Connection reset by peer (fd={}).", io_state.fd.get());
                    return -1;  // EOF
                }

                logger_->error("Failed to read msg header (fd={}, err={}): {}.",
                               io_state.fd.get(),
                               err,
                               std::strerror(err));
                return err;
            }

            // Progress the partial read state.
            //
            io_state.rx_partial_size += bytes_read;
            CETL_DEBUG_ASSERT(io_state.rx_partial_size <= sizeof(msg_header), "");
            if (bytes_read == 0)
            {
                logger_->debug("Zero bytes of msg header read - end of stream (fd={}).", io_state.fd.get());
                return -1;  // EOF
            }
            if (io_state.rx_partial_size < sizeof(msg_header))
            {
                // Not enough data yet - that's ok, the next attempt will try to read the rest.
                return 0;
            }

            // Validate the message header.
            // Just in case validate also the payload size to be within the reasonable limits.
            // Zero payload size is also considered invalid (b/c we always expect non-empty `Route` payload).
            //
            if ((msg_header.signature != MsgHeaderSignature)  //
                || (msg_header.payload_size == 0) || (msg_header.payload_size > MsgPayloadMaxSize))
            {
                logger_->error("Invalid msg header read - closing invalid stream (fd={}, payload_size={}).",
                               io_state.fd.get(),
                               msg_header.payload_size);
                return EINVAL;
            }
        }

        // Message header has been read and validated.
        // Switch to the next part - message payload.
        //
        io_state.rx_partial_size = 0;
        auto payload_buffer = std::make_unique<std::uint8_t[]>(msg_header.payload_size);  // NOLINT(*-avoid-c-arrays)
        io_state.rx_msg_part.emplace<IoState::MsgPayload>(
            IoState::MsgPayload{msg_header.payload_size, std::move(payload_buffer)});
    }

    // 2. Read message payload.
    //
    if (auto* const msg_payload_ptr = cetl::get_if<IoState::MsgPayload>(&io_state.rx_msg_part))
    {
        auto& msg_payload = *msg_payload_ptr;

        CETL_DEBUG_ASSERT(io_state.rx_partial_size < msg_payload.size, "");
        if (io_state.rx_partial_size < msg_payload.size)
        {
            ssize_t bytes_read = 0;
            if (const auto err = platform::posixSyscallError([&io_state, &bytes_read, &msg_payload] {
                    //
                    std::uint8_t* const dst_buf = msg_payload.buffer.get() + io_state.rx_partial_size;
                    //
                    const auto bytes_to_read = msg_payload.size - io_state.rx_partial_size;
                    return bytes_read        = ::recv(io_state.fd.get(), dst_buf, bytes_to_read, MSG_DONTWAIT);
                }))
            {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                {
                    // No data available yet - that's ok, the next attempt will try to read again.
                    //
                    logger_->trace("Msg payload read would block (fd={}).", io_state.fd.get());
                    return 0;
                }
                logger_->error("Failed to read msg payload (fd={}): {}.", io_state.fd.get(), std::strerror(err));
                return err;
            }

            // Progress the partial read state.
            //
            io_state.rx_partial_size += bytes_read;
            CETL_DEBUG_ASSERT(io_state.rx_partial_size <= msg_payload.size, "");
            if (bytes_read == 0)
            {
                logger_->debug("Zero bytes of msg payload read - end of stream (fd={}).", io_state.fd.get());
                return -1;  // EOF
            }
            if (io_state.rx_partial_size < msg_payload.size)
            {
                // Not enough data yet - that's ok, the next attempt will try to read the rest.
                return 0;
            }
        }

        // Message payload has been completely received.
        // Switch to the first part - the message header again.
        //
        io_state.rx_partial_size = 0;
        const auto payload       = std::move(msg_payload);
        io_state.rx_msg_part.emplace<IoState::MsgHeader>();

        io_state.on_rx_msg_payload(Payload{payload.buffer.get(), payload.size});
    }

    return 0;
}

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd
