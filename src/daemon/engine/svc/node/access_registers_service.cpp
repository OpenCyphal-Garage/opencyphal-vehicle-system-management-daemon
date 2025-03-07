//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "access_registers_service.hpp"

#include "engine_helpers.hpp"
#include "ipc/channel.hpp"
#include "ipc/server_router.hpp"
#include "logging.hpp"
#include "svc/node/access_registers_spec.hpp"
#include "svc/svc_helpers.hpp"

#include <uavcan/_register/Access_1_0.hpp>

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

/// Defines 'Node: Access Registers' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class AccessRegistersServiceImpl final
{
public:
    using Spec    = common::svc::node::AccessRegistersSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit AccessRegistersServiceImpl(const ScvContext& context)
        : context_{context}
    {
    }

    /// Handles the initial `node::AccessRegisters` service request of a new IPC channel.
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
    //    and Cyphal `Access` request is sent to each of them.
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

        Fsm(AccessRegistersServiceImpl& service, const Id id, Channel&& channel)
            : id_{id}
            , channel_{std::move(channel)}
            , service_{service}
            , processing_{false}
        {
            logger().trace("AccessRegsSvc::Fsm (id={}).", id_);

            channel_.subscribe([this](const auto& event_var) {
                //
                cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
            });
        }

        ~Fsm()
        {
            logger().trace("AccessRegsSvc::~Fsm (id={}).", id_);
        }

        Fsm(const Fsm&)                = delete;
        Fsm(Fsm&&) noexcept            = delete;
        Fsm& operator=(const Fsm&)     = delete;
        Fsm& operator=(Fsm&&) noexcept = delete;

        void start(const Spec::Request& request)
        {
            handleEvent(request);
        }

    private:
        using RegKeyValue = common::svc::node::AccessRegistersKeyValue_0_1;
        using RegKey      = RegKeyValue::_traits_::TypeOf::key;
        using RegValue    = RegKeyValue::_traits_::TypeOf::value;

        using CyRegAccessSvc   = uavcan::_register::Access_1_0;
        using CySvcClient      = libcyphal::presentation::ServiceClient<CyRegAccessSvc>;
        using CyPromise        = libcyphal::presentation::ResponsePromise<CyRegAccessSvc::Response>;
        using CyPromiseFailure = libcyphal::presentation::ResponsePromiseFailure;

        struct NodeContext
        {
            libcyphal::Duration         timeout;
            std::uint16_t               reg_index{0};
            cetl::optional<CySvcClient> client;
            cetl::optional<CyPromise>   promise;
        };

        common::Logger& logger() const
        {
            return *service_.logger_;
        }

        cetl::pmr::memory_resource& memory() const
        {
            return service_.context_.memory;
        }

        // We are not interested in handling this event.
        static void handleEvent(const Channel::Connected&) {}

        void handleEvent(const Channel::Input& input)
        {
            CETL_DEBUG_ASSERT(!processing_, "");
            if (processing_)
            {
                logger().warn("AccessRegsSvc: Ignoring extra input - already processing (id={}).", id_);
                return;
            }

            cetl::visit([this](const auto& event) { handleInputEvent(event); }, input.union_value);
        }

        static void handleInputEvent(const Channel::Input::_traits_::TypeOf::empty) {}

        void handleInputEvent(const Channel::Input::_traits_::TypeOf::scope& scope_input)
        {
            const auto timeout = std::chrono::duration_cast<libcyphal::Duration>(  //
                std::chrono::microseconds{scope_input.timeout_us});

            for (const auto node_id : scope_input.node_ids)
            {
                node_id_to_cnxt_.emplace(node_id, NodeContext{timeout});
            }
        }

        void handleInputEvent(const Channel::Input::_traits_::TypeOf::_register& register_input)
        {
            registers_.emplace_back(register_input);
        }

        void handleEvent(const Channel::Completed& completed)
        {
            logger().debug("AccessRegsSvc::Fsm::handleEvent({}) (id={}).", completed, id_);

            if (!completed.keep_alive)
            {
                logger().warn("AccessRegsSvc: canceling processing (id={}).", id_);
                complete(ECANCELED);
                return;
            }

            if (processing_)
            {
                logger().warn("AccessRegsSvc: Ignoring extra channel completion - already processing (id={}).", id_);
                return;
            }
            processing_ = true;

            if (node_id_to_cnxt_.empty() || registers_.empty())
            {
                logger().debug("AccessRegsSvc: Nothing to do - empty working set (id={}, nodes={}, regs={}).",
                               id_,
                               node_id_to_cnxt_.size(),
                               registers_.size());
                complete(0);
                return;
            }

            // Below `makeCyRpcClient` call might modify `node_id_to_cnxt_`,
            // so we need to collect all node ids first.
            //
            std::vector<std::uint16_t> node_ids;
            node_ids.reserve(node_id_to_cnxt_.size());
            for (const auto& pair : node_id_to_cnxt_)
            {
                node_ids.push_back(pair.first);
            }

            // For each node we try initiate the 1st register RPC access.
            // On RPC replies, we will increment register index and repeat until all registers are processed.
            //
            for (const auto node_id : node_ids)
            {
                makeCyRpcClient(node_id);
            }
        }

        void makeCyRpcClient(const std::uint16_t node_id)
        {
            using CyMakeFailure = libcyphal::presentation::Presentation::MakeFailure;

            const auto it = node_id_to_cnxt_.find(node_id);
            if (it == node_id_to_cnxt_.end())
            {
                return;
            }
            auto& node_cnxt = it->second;

            auto cy_make_result = service_.context_.presentation.makeClient<CyRegAccessSvc>(node_id);
            if (const auto* cy_failure = cetl::get_if<CyMakeFailure>(&cy_make_result))
            {
                const auto err = failureToErrorCode(*cy_failure);
                logger().warn("AccessRegsSvc: failed to make RPC client for node {} (err={}, fsm_id={}).",
                              node_id,
                              err,
                              id_);

                sendResponse(node_id, RegKeyValue{&memory()}, err);
                releaseNodeContext(node_id);
                return;
            }
            node_cnxt.client.emplace(cetl::get<CySvcClient>(std::move(cy_make_result)));

            startCyRegAccessRpcCallFor(node_id, node_cnxt);
        }

        void startCyRegAccessRpcCallFor(const std::uint16_t node_id, NodeContext& node_cnxt)
        {
            CETL_DEBUG_ASSERT(processing_, "");
            CETL_DEBUG_ASSERT(node_cnxt.client, "");

            while (node_cnxt.reg_index < registers_.size())
            {
                const auto  deadline = service_.context_.executor.now() + node_cnxt.timeout;
                const auto& reg      = registers_[node_cnxt.reg_index];

                const CyRegAccessSvc::Request cy_request{reg.key, reg.value, &memory()};

                auto cy_req_result = node_cnxt.client->request(deadline, cy_request);
                if (auto* const cy_promise = cetl::get_if<CyPromise>(&cy_req_result))
                {
                    cy_promise->setCallback([this, node_id](const auto& arg) {
                        //
                        handleNodeResponse(node_id, arg.result);
                    });
                    node_cnxt.promise.emplace(std::move(*cy_promise));
                    return;
                }

                const auto cy_failure = cetl::get<CySvcClient::Failure>(std::move(cy_req_result));
                const auto err        = failureToErrorCode(cy_failure);
                logger().warn("AccessRegsSvc: failed to send RPC request to node {} (err={}, fsm_id={})",
                              node_id,
                              err,
                              id_);

                sendResponse(node_id, RegKeyValue{reg.key, RegValue{&memory()}, &memory()}, err);

                node_cnxt.reg_index++;

            }  // while

            releaseNodeContext(node_id);
        }

        void handleNodeResponse(const std::uint16_t node_id, const CyPromise::Result& result)
        {
            const auto it = node_id_to_cnxt_.find(node_id);
            if (it == node_id_to_cnxt_.end())
            {
                return;
            }
            auto& node_cnxt = it->second;

            CETL_DEBUG_ASSERT(processing_, "");
            CETL_DEBUG_ASSERT(node_cnxt.reg_index < registers_.size(), "");

            int         err_code = 0;
            const auto& reg      = registers_[node_cnxt.reg_index];
            RegKeyValue reg_key_value{reg.key, RegValue{&memory()}, &memory()};
            //
            if (const auto* success = cetl::get_if<CyPromise::Success>(&result))
            {
                reg_key_value.value = success->response.value;
            }
            else if (const auto* cy_failure = cetl::get_if<CyPromiseFailure>(&result))
            {
                err_code = failureToErrorCode(*cy_failure);
                logger().warn("ListRegsSvc: RPC promise failure for node {} (err={}, fsm_id={}).",
                              node_id,
                              err_code,
                              id_);
            }
            sendResponse(node_id, reg_key_value, err_code);

            node_cnxt.reg_index++;
            startCyRegAccessRpcCallFor(node_id, node_cnxt);
        }

        void sendResponse(const std::uint16_t node_id, const RegKeyValue& reg, const int err_code = 0)
        {
            Spec::Response ipc_response{&memory()};
            ipc_response.error_code = err_code;
            ipc_response.node_id    = node_id;
            ipc_response._register  = reg;

            if (const auto err = channel_.send(ipc_response))
            {
                logger().warn("AccessRegsSvc: failed to send ipc response for node {} (err={}, fsm_id={}).",
                              node_id,
                              err,
                              id_);
            }
        }

        void releaseNodeContext(const std::uint16_t node_id)
        {
            node_id_to_cnxt_.erase(node_id);
            if (node_id_to_cnxt_.empty())
            {
                complete(0);
            }
        }

        void complete(const int err_code)
        {
            // Cancel anything that might be still pending.
            node_id_to_cnxt_.clear();

            if (const auto err = channel_.complete(err_code))
            {
                logger().warn("AccessRegsSvc: failed to complete channel (err={}, fsm_id={}).", err, id_);
            }

            service_.releaseFsmBy(id_);
        }

        const Id                                       id_;
        Channel                                        channel_;
        AccessRegistersServiceImpl&                    service_;
        bool                                           processing_;
        std::vector<RegKeyValue>                       registers_;
        std::unordered_map<std::uint16_t, NodeContext> node_id_to_cnxt_;

    };  // Fsm

    void releaseFsmBy(const Fsm::Id fsm_id)
    {
        id_to_fsm_.erase(fsm_id);
    }

    const ScvContext                      context_;
    std::uint64_t                         next_fsm_id_{0};
    std::unordered_map<Fsm::Id, Fsm::Ptr> id_to_fsm_;
    common::LoggerPtr                     logger_{common::getLogger("engine")};

};  // AccessRegistersServiceImpl

}  // namespace

void AccessRegistersService::registerWithContext(const ScvContext& context)
{
    using Impl = AccessRegistersServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context});
}

}  // namespace node
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
