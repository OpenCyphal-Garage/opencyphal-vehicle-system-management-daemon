//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_SVC_FILE_SERVER_POP_ROOT_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_SVC_FILE_SERVER_POP_ROOT_CLIENT_HPP_INCLUDED

#include "ocvsmd/sdk/defines.hpp"
#include "svc/client_helpers.hpp"
#include "svc/file_server/pop_root_spec.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace file_server
{

/// Defines interface of the 'File Server: Pop Root' service client.
///
class PopRootClient
{
public:
    using Ptr  = std::shared_ptr<PopRootClient>;
    using Spec = common::svc::file_server::PopRootSpec;

    using Success = cetl::monostate;
    using Failure = Error;
    using Result  = cetl::variant<Success, Failure>;

    CETL_NODISCARD static Ptr make(const ClientContext& context, const Spec::Request& request);

    PopRootClient(PopRootClient&&)                 = delete;
    PopRootClient(const PopRootClient&)            = delete;
    PopRootClient& operator=(PopRootClient&&)      = delete;
    PopRootClient& operator=(const PopRootClient&) = delete;

    virtual ~PopRootClient() = default;

    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
        submitImpl([receive = std::forward<Receiver>(receiver)](Result&& result) mutable {
            //
            receive(std::move(result));
        });
    }

protected:
    PopRootClient() = default;

    virtual void submitImpl(std::function<void(Result&&)>&& receiver) = 0;

};  // PopRootClient

}  // namespace file_server
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_FILE_SERVER_POP_ROOT_CLIENT_HPP_INCLUDED
