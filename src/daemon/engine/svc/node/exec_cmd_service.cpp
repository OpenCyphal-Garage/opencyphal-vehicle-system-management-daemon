//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "exec_cmd_service.hpp"

#include "engine_helpers.hpp"
#include "ipc/channel.hpp"
#include "ipc/server_router.hpp"
#include "logging.hpp"
#include "svc/node/exec_cmd_spec.hpp"
#include "svc/svc_helpers.hpp"

#include <uavcan/node/ExecuteCommand_1_3.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/presentation/client.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/presentation/response_promise.hpp>

#include <cerrno>
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

/// Defines 'Node: Execute Command' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class ExecCmdServiceImpl final
{
public:
    using Spec    = common::svc::node::ExecCmdSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit ExecCmdServiceImpl(const ScvContext& context)
        : context_{context}
    {
    }

    /// Handles the initial `node::ExecCmd` service request of a new IPC channel.
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
    // Defines private Finite State Machine (FSM) which tracks the progress of a single service request.
    // There is one FSM per each service request channel.
    //
    // 1. On its `start` a set of Cyphal RPC clients is created (one per each node ID in the request),
    //    and a command request is sent to each of them.
    // 2. Then, the FSM waits for the RPC responses from the Cyphal nodes, collecting them.
    // 3. Finally, FSM sends the accumulated result (when all responses are received or timed out)
    //    and completes the channel.
    //
    class Fsm final
    {
    public:
        using Id  = std::uint64_t;
        using Ptr = std::shared_ptr<Fsm>;

        Fsm(ExecCmdServiceImpl& service, const Id id, Channel&& channel)
            : id_{id}
            , channel_{std::move(channel)}
            , service_{service}
        {
            logger().trace("ExecCmdSvc::Fsm (id={}).", id_);

            channel_.subscribe([this](const auto& event_var) {
                //
                cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
            });
        }

        ~Fsm()
        {
            logger().trace("ExecCmdSvc::~Fsm (id={}).", id_);
        }

        Fsm(const Fsm&)                = delete;
        Fsm(Fsm&&) noexcept            = delete;
        Fsm& operator=(const Fsm&)     = delete;
        Fsm& operator=(Fsm&&) noexcept = delete;

        void start(const Spec::Request& request)
        {
            logger().trace("ExecCmdSvc::Fsm::start (fsm_id={}, timeout={}us).", id_, request.timeout_us);

            // Immediately complete if there are no nodes to execute the command on.
            //
            if (request.node_ids.empty())
            {
                complete(0);
                return;
            }

            // It's ok to have duplicates in the request -
            // we just ignore duplicates, and work with unique ones.
            const SetOfNodeIds unique_node_ids{request.node_ids.begin(), request.node_ids.end()};

            const auto deadline = service_.context_.executor.now() + std::chrono::microseconds{request.timeout_us};
            const CyExecCmdSvc::Request cy_request{request.payload.command, request.payload.parameter, &memory()};
            for (const auto node_id : unique_node_ids)
            {
                if (const auto err = makeCySvcCallFor(deadline, node_id, cy_request))
                {
                    complete(err);
                    return;
                }
            }
        }

    private:
        using SetOfNodeIds = std::unordered_set<std::uint16_t>;

        using CyExecCmdSvc     = uavcan::node::ExecuteCommand_1_3;
        using CySvcClient      = libcyphal::presentation::ServiceClient<CyExecCmdSvc>;
        using CyPromise        = libcyphal::presentation::ResponsePromise<CyExecCmdSvc::Response>;
        using CyPromiseFailure = libcyphal::presentation::ResponsePromiseFailure;

        struct CyNodeOp
        {
            CySvcClient client;
            CyPromise   promise;
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
            logger().debug("ExecCmdSvc::Fsm::handleEvent({}) (id={}).", completed, id_);
            complete(ECANCELED);
        }

        int makeCySvcCallFor(const libcyphal::TimePoint   deadline,
                             const std::uint16_t          node_id,
                             const CyExecCmdSvc::Request& cy_request)
        {
            using CyMakeFailure = libcyphal::presentation::Presentation::MakeFailure;

            auto cy_make_result = service_.context_.presentation.makeClient<CyExecCmdSvc>(node_id);
            if (const auto* cy_failure = cetl::get_if<CyMakeFailure>(&cy_make_result))
            {
                const auto err = failureToErrorCode(*cy_failure);
                logger().error("ExecCmdSvc: failed to make RPC client for node {} (err={}, fsm_id={}).",
                               node_id,
                               err,
                               id_);
                return err;
            }
            auto cy_svc_client = cetl::get<CySvcClient>(std::move(cy_make_result));

            auto cy_req_result = cy_svc_client.request(deadline, cy_request);
            if (const auto* cy_failure = cetl::get_if<CySvcClient::Failure>(&cy_req_result))
            {
                const auto err = failureToErrorCode(*cy_failure);
                logger().error("ExecCmdSvc: failed to send RPC request to node {} (err={}, fsm_id={})",
                               node_id,
                               err,
                               id_);
                return err;
            }
            auto cy_promise = cetl::get<CyPromise>(std::move(cy_req_result));

            cy_promise.setCallback([this, node_id](const auto& arg) {
                //
                if (const auto* cy_failure = cetl::get_if<CyPromiseFailure>(&arg.result))
                {
                    const auto err = failureToErrorCode(*cy_failure);
                    logger().warn("ExecCmdSvc: RPC promise failure for node {} (err={}, fsm_id={}).",
                                  node_id,
                                  err,
                                  id_);
                }
                else if (const auto* success = cetl::get_if<CyPromise::Success>(&arg.result))
                {
                    const auto& res = success->response;
                    logger().debug("ExecCmdSvc: RPC promise success from node {} (status={}, fsm_id={}).",
                                   node_id,
                                   res.status,
                                   id_);

                    const Spec::Response ipc_response{node_id, {res.status, res.output, &memory()}, &memory()};
                    if (const auto err = channel_.send(ipc_response))
                    {
                        logger().warn("ExecCmdSvc: failed to send ipc response for node {} (err={}, fsm_id={}).",
                                      node_id,
                                      err,
                                      id_);
                    }
                }

                // We've got the response from the node, so we can release associated resources (client & promise).
                // If no nodes left, then it means we did it for all nodes, so the whole FSM is completed.
                //
                node_id_to_op_.erase(node_id);
                if (node_id_to_op_.empty())
                {
                    complete(0);
                }
            });

            node_id_to_op_.emplace(node_id, CyNodeOp{std::move(cy_svc_client), std::move(cy_promise)});
            return 0;
        }

        void complete(const int err_code)
        {
            // Cancel anything that might be still pending.
            node_id_to_op_.clear();

            if (const auto err = channel_.complete(err_code))
            {
                logger().warn("ExecCmdSvc: failed to complete channel (err={}, fsm_id={}).", err, id_);
            }

            service_.releaseFsmBy(id_);
        }

        const Id                                    id_;
        Channel                                     channel_;
        ExecCmdServiceImpl&                         service_;
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

};  // ExecCmdServiceImpl

}  // namespace

void ExecCmdService::registerWithContext(const ScvContext& context)
{
    using Impl = ExecCmdServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context});
}

}  // namespace node
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
