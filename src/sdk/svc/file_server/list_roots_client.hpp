//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_SVC_FILE_SERVER_LIST_ROOTS_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_SVC_FILE_SERVER_LIST_ROOTS_CLIENT_HPP_INCLUDED

#include "ipc/client_router.hpp"
#include "svc/file_server/list_roots_spec.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace file_server
{

/// Defines interface of the 'File Server: List Roots' service client.
///
class ListRootsClient
{
public:
    using Ptr  = std::shared_ptr<ListRootsClient>;
    using Spec = common::svc::file_server::ListRootsSpec;

    using Success = std::vector<std::string>;
    using Failure = int;  // `errno`-like error code
    using Result  = cetl::variant<Success, Failure>;

    CETL_NODISCARD static Ptr make(cetl::pmr::memory_resource&           memory,
                                   const common::ipc::ClientRouter::Ptr& ipc_router,
                                   const Spec::Request&                  request);

    ListRootsClient(ListRootsClient&&)                 = delete;
    ListRootsClient(const ListRootsClient&)            = delete;
    ListRootsClient& operator=(ListRootsClient&&)      = delete;
    ListRootsClient& operator=(const ListRootsClient&) = delete;

    virtual ~ListRootsClient() = default;

    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
        submitImpl([receive = std::forward<Receiver>(receiver)](Result&& result) mutable {
            //
            receive(std::move(result));
        });
    }

protected:
    ListRootsClient() = default;

    virtual void submitImpl(std::function<void(Result&&)>&& receiver) = 0;

};  // ListRootsClient

}  // namespace file_server
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_FILE_SERVER_LIST_ROOTS_CLIENT_HPP_INCLUDED
