//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "list_registers_service.hpp"

#include "engine_helpers.hpp"
#include "ipc/channel.hpp"
#include "ipc/server_router.hpp"
#include "logging.hpp"
#include "svc/node/list_registers_spec.hpp"
#include "svc/svc_helpers.hpp"

#include <uavcan/_register/List_1_0.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/presentation/client.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/presentation/response_promise.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace svc
{
namespace node
{
namespace
{

/// Defines 'Node: List Registers' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class ListRegistersServiceImpl final
{
public:
    using Spec    = common::svc::node::ListRegistersSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit ListRegistersServiceImpl(const ScvContext& context)
        : context_{context}
    {
    }

    /// Handles the initial `node::ListRegisters` service request of a new IPC channel.
    ///
    /// Defined as a functor operator - as it's required/expected by the IPC server router.
    ///
    void operator()(Channel&& channel, const Spec::Request& request)
    {
        const auto fsm_id = next_fsm_id_++;
        logger_->debug("New '{}' service channel (fsm={}).", Spec::svc_full_name(), fsm_id);

        auto fsm           = std::make_shared<Fsm>(*this, fsm_id, std::move(channel));
        id_to_fsm_[fsm_id] = fsm;

        fsm->start(request);
    }

private:
    // Defines private Finite State Machine (FSM) which tracks the progress of a single IPC service request.
    // There is one FSM per each service request channel.
    //
    // 1. On its `start` a set of Cyphal RPC clients is created (one per each node ID in the request),
    //    and Cyphal `List` request is sent to each of them.
    // 2. On RPC response reception FSM forwards a node response to the IPC client (which accumulates responses), and
    //    then repeats steps 1-2 for the incremented index in the registry list (until empty response from the node).
    // 3. Finally, when the working set of nodes becomes empty (all nodes returned their entire list of registers),
    //    FSM completes the channel.
    //
    class Fsm final
    {
    public:
        using Id  = std::uint64_t;
        using Ptr = std::shared_ptr<Fsm>;

        Fsm(ListRegistersServiceImpl& service, const Id id, Channel&& channel)
            : id_{id}
            , channel_{std::move(channel)}
            , service_{service}
            , timeout_{}
        {
            logger().trace("ListRegsSvc::Fsm (id={}).", id_);

            channel_.subscribe([this](const auto& event_var) {
                //
                cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
            });
        }

        ~Fsm()
        {
            logger().trace("ListRegsSvc::~Fsm (id={}).", id_);
        }

        Fsm(const Fsm&)                = delete;
        Fsm(Fsm&&) noexcept            = delete;
        Fsm& operator=(const Fsm&)     = delete;
        Fsm& operator=(Fsm&&) noexcept = delete;

        void start(const Spec::Request& request)
        {
            logger().trace("ListRegsSvc::Fsm::start (fsm_id={}, timeout={}us).", id_, request.timeout_us);

            timeout_ = std::chrono::duration_cast<libcyphal::Duration>(std::chrono::microseconds{request.timeout_us});

            // It's ok to have duplicates in the request -
            // we just ignore duplicates, and work with unique ones.
            const SetOfNodeIds unique_node_ids{request.node_ids.begin(), request.node_ids.end()};
            for (const auto node_id : unique_node_ids)
            {
                makeCyNodeOp(node_id);
            }

            if (node_id_to_op_.empty())
            {
                complete(0);
            }
        }

    private:
        using SetOfNodeIds         = std::unordered_set<std::uint16_t>;
        using CyphalRegListSvc     = uavcan::_register::List_1_0;
        using CyphalSvcClient      = libcyphal::presentation::ServiceClient<CyphalRegListSvc>;
        using CyphalPromise        = libcyphal::presentation::ResponsePromise<CyphalRegListSvc::Response>;
        using CyphalPromiseFailure = libcyphal::presentation::ResponsePromiseFailure;

        struct CyNodeOp
        {
            std::uint16_t                 index;
            CyphalSvcClient               client;
            cetl::optional<CyphalPromise> promise;
        };

        common::Logger& logger() const
        {
            return *service_.logger_;
        }

        cetl::pmr::memory_resource& memory() const
        {
            return service_.context_.memory;
        }

        // We are not interested in handling these events.
        static void handleEvent(const Channel::Connected&) {}
        static void handleEvent(const Channel::Input&) {}

        void handleEvent(const Channel::Completed& completed)
        {
            logger().debug("ListRegsSvc::Fsm::handleEvent({}) (id={}).", completed, id_);
            complete(ECANCELED);
        }

        void makeCyNodeOp(const std::uint16_t node_id)
        {
            using CyphalMakeFailure = libcyphal::presentation::Presentation::MakeFailure;

            auto cy_make_result = service_.context_.presentation.makeClient<CyphalRegListSvc>(node_id);
            if (const auto* cy_failure = cetl::get_if<CyphalMakeFailure>(&cy_make_result))
            {
                const auto err = failureToErrorCode(*cy_failure);
                logger().error("ListRegsSvc: failed to make svc client for node {} (err={}, fsm_id={}).",
                               node_id,
                               err,
                               id_);

                sendErrorResponse(node_id, err);
                return;
            }

            auto it = node_id_to_op_
                          .emplace(  //
                              node_id,
                              CyNodeOp{0, cetl::get<CyphalSvcClient>(std::move(cy_make_result)), cetl::nullopt})
                          .first;

            startCyRegListRpcCallFor(node_id, it->second);
        }

        bool startCyRegListRpcCallFor(const std::uint16_t node_id, CyNodeOp& cy_op)
        {
            const CyphalRegListSvc::Request cy_request{cy_op.index, &memory()};

            const auto deadline      = service_.context_.executor.now() + timeout_;
            auto       cy_req_result = cy_op.client.request(deadline, cy_request);
            if (const auto* cy_failure = cetl::get_if<CyphalSvcClient::Failure>(&cy_req_result))
            {
                const auto err = failureToErrorCode(*cy_failure);
                logger().error("ListRegsSvc: failed to send svc request to node {} (err={}, fsm_id={})",
                               node_id,
                               err,
                               id_);

                sendErrorResponse(node_id, err);
                return false;
            }
            auto cy_promise = cetl::get<CyphalPromise>(std::move(cy_req_result));

            cy_promise.setCallback([this, node_id](const auto& arg) {
                //
                handleNodeResponse(node_id, arg.result);
            });

            cy_op.promise.emplace(std::move(cy_promise));
            return true;
        }

        void handleNodeResponse(const std::uint16_t node_id, const CyphalPromise::Result& result)
        {
            const auto it = node_id_to_op_.find(node_id);
            if (it == node_id_to_op_.end())
            {
                return;
            }
            auto& cy_op = it->second;

            if (const auto* success = cetl::get_if<CyphalPromise::Success>(&result))
            {
                // Empty response name means that we've reached the end of the list.
                //
                const auto& res = success->response;
                if (!res.name.name.empty())
                {
                    // Forward intermediate response to the IPC client,
                    // and start the next RPC call for the same node (with ++index).
                    //
                    const Spec::Response ipc_response{0, node_id, res.name, &memory()};
                    if (const auto err = channel_.send(ipc_response))
                    {
                        logger().warn("ListRegsSvc: failed to send ipc response for node {} (err={}, fsm_id={}).",
                                      node_id,
                                      err,
                                      id_);
                    }
                    else
                    {
                        cy_op.index++;
                        if (startCyRegListRpcCallFor(node_id, cy_op))
                        {
                            return;
                        }
                    }
                }
            }
            else if (const auto* cy_failure = cetl::get_if<CyphalPromiseFailure>(&result))
            {
                const auto err = failureToErrorCode(*cy_failure);
                logger().warn("ListRegsSvc: promise failure for node {} (err={}, fsm_id={}).", node_id, err, id_);
                sendErrorResponse(node_id, err);
            }

            // We've got an empty response from the node (or there was an error).
            // So we can release associated resources (client and promise).
            // If no nodes left, then it means we did it for all nodes, so the whole FSM is completed.
            //
            node_id_to_op_.erase(it);
            if (node_id_to_op_.empty())
            {
                complete(0);
            }
        }

        // TODO: Fix nolint by moving from `int` to `ErrorCode`.
        // NOLINTNEXTLINE bugprone-easily-swappable-parameters
        void sendErrorResponse(const std::uint16_t node_id, const int err_code)
        {
            Spec::Response ipc_response{&memory()};
            ipc_response.error_code = err_code;
            ipc_response.node_id    = node_id;

            if (const auto err = channel_.send(ipc_response))
            {
                logger().warn(  //
                    "ListRegsSvc: failed to send ipc failure (err_code={}) response for node {} (err={}, fsm_id={}).",
                    err_code,
                    node_id,
                    err,
                    id_);
            }
        }

        void complete(const int err)
        {
            // Cancel anything that might be still pending.
            node_id_to_op_.clear();

            channel_.complete(err);

            service_.releaseFsmBy(id_);
        }

        const Id                                    id_;
        Channel                                     channel_;
        ListRegistersServiceImpl&                   service_;
        libcyphal::Duration                         timeout_;
        std::unordered_map<std::uint16_t, CyNodeOp> node_id_to_op_;

    };  // Fsm

    void releaseFsmBy(const Fsm::Id fsm_id)
    {
        id_to_fsm_.erase(fsm_id);
    }

    const ScvContext                      context_;
    std::uint64_t                         next_fsm_id_{0};
    std::unordered_map<Fsm::Id, Fsm::Ptr> id_to_fsm_;
    common::LoggerPtr                     logger_{common::getLogger("engine")};

};  // ListRegistersServiceImpl

}  // namespace

void ListRegistersService::registerWithContext(const ScvContext& context)
{
    using Impl = ListRegistersServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context});
}

}  // namespace node
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
