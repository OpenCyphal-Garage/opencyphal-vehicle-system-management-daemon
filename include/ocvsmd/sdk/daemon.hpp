//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_DAEMON_HPP_INCLUDED
#define OCVSMD_SDK_DAEMON_HPP_INCLUDED

#include <memory>

namespace ocvsmd
{
namespace sdk
{

/// An abstract factory for the specialized interfaces.
///
class Daemon
{
public:
    static std::unique_ptr<Daemon> make();

    Daemon(Daemon&&)                 = delete;
    Daemon(const Daemon&)            = delete;
    Daemon& operator=(Daemon&&)      = delete;
    Daemon& operator=(const Daemon&) = delete;

    virtual ~Daemon() = default;

    virtual void send_messages() const = 0;

protected:
    Daemon() = default;

};  // Daemon

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_DAEMON_HPP_INCLUDED
