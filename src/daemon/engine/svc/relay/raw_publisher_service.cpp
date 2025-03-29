//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "raw_publisher_service.hpp"

#include "common_helpers.hpp"
#include "engine_helpers.hpp"
#include "logging.hpp"
#include "svc/relay/raw_publisher_spec.hpp"
#include "svc/svc_helpers.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/visit_helpers.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/presentation/publisher.hpp>

#include <array>
#include <memory>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace svc
{
namespace relay
{
namespace
{

/// Defines 'Relay: Raw Publisher' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class RawPublisherServiceImpl final
{
public:
    using Spec    = common::svc::relay::RawPublisherSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit RawPublisherServiceImpl(const ScvContext& context)
        : context_{context}
    {
    }

    /// Handles the initial `relay::RawPublisher` service request of a new IPC channel.
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
    class Fsm final
    {
    public:
        using Id  = std::uint64_t;
        using Ptr = std::shared_ptr<Fsm>;

        Fsm(RawPublisherServiceImpl& service, const Id id, Channel&& channel)
            : id_{id}
            , channel_{std::move(channel)}
            , service_{service}
        {
            logger().trace("RawPublisherSvc::Fsm (id={}).", id_);

            channel_.subscribe([this](const auto& event_var, const auto payload) {
                //
                cetl::visit(                //
                    cetl::make_overloaded(  //
                        [this, payload](const Channel::Input& input) {
                            //
                            handleEvent(input, payload);
                        },
                        [this](const Channel::Completed& completed) {
                            //
                            handleEvent(completed);
                        },
                        [this](const Channel::Connected&) {}),
                    event_var);
            });
        }

        ~Fsm() = default;

        Fsm(const Fsm&)                = delete;
        Fsm(Fsm&&) noexcept            = delete;
        Fsm& operator=(const Fsm&)     = delete;
        Fsm& operator=(Fsm&&) noexcept = delete;

        void start(const Spec::Request& request)
        {
            constexpr auto CreateReq = Spec::Request::VariantType::IndexOf::create;

            if (const auto* const create_req = cetl::get_if<CreateReq>(&request.union_value))
            {
                if (makeCyPublisher(create_req->subject_id))
                {
                    const Spec::Response ipc_response{&memory()};
                    if (const auto opt_error = channel_.send(ipc_response))
                    {
                        logger().warn("RawPublisherSvc: failed to send ipc reply (err={}, fsm_id={}).",
                                      *opt_error,
                                      id_);
                        complete(opt_error);
                    }
                }
            }
        }

    private:
        using RawPublisherCreate  = common::svc::relay::RawPublisherCreate_0_1;
        using RawPublisherConfig  = common::svc::relay::RawPublisherConfig_0_1;
        using RawPublisherPublish = common::svc::relay::RawPublisherPublish_0_1;

        using CyPayloadFragment = libcyphal::transport::PayloadFragment;
        using CyRawPublisher    = libcyphal::presentation::Publisher<void>;

        common::Logger& logger() const
        {
            return *service_.logger_;
        }

        cetl::pmr::memory_resource& memory() const
        {
            return service_.context_.memory;
        }

        void handleEvent(const Channel::Input& input, const common::io::Payload payload)
        {
            logger().trace("RawPublisherSvc::handleEvent(Input).");

            cetl::visit(                //
                cetl::make_overloaded(  //
                    [this](const RawPublisherConfig& config) {
                        //
                        handleInputEvent(config);
                    },
                    [this, payload](const RawPublisherPublish& publish) {
                        //
                        handleInputEvent(publish, payload);
                    },
                    [](const RawPublisherCreate&) {},
                    [](const uavcan::primitive::Empty_1_0&) {}),
                input.union_value);
        }

        void handleEvent(const Channel::Completed& completed)
        {
            logger().debug("RawPublisherSvc::handleEvent({}) (fsm_id={}).", completed, id_);

            if (!completed.keep_alive)
            {
                complete(completed.opt_error);
            }
        }

        void handleInputEvent(const common::svc::relay::RawPublisherConfig_0_1& config)
        {
            CETL_DEBUG_ASSERT(cy_raw_publisher_, "");
            if (!cy_raw_publisher_)
            {
                complete(sdk::Error{sdk::Error::Code::Canceled});
                return;
            }

            if (!config.priority.empty())
            {
                cy_raw_publisher_->setPriority(convertToCyPriority(config.priority.front()));
            }
        }

        void handleInputEvent(const common::svc::relay::RawPublisherPublish_0_1 publish,
                              const common::io::Payload                         payload)
        {
            CETL_DEBUG_ASSERT(cy_raw_publisher_, "");
            if (!cy_raw_publisher_)
            {
                complete(sdk::Error{sdk::Error::Code::Canceled});
                return;
            }

            const auto timeout  = std::chrono::duration_cast<libcyphal::Duration>(  //
                std::chrono::microseconds{publish.timeout_us});
            const auto deadline = service_.context_.executor.now() + timeout;

            // The tail of the payload is the raw message data.
            //
            const auto                       raw_msg_payload = payload.subspan(payload.size() - publish.payload_size);
            std::array<CyPayloadFragment, 1> fragments{{{raw_msg_payload.data(), raw_msg_payload.size()}}};

            sdk::OptError opt_error;
            if (const auto cy_failure = cy_raw_publisher_->publish(deadline, fragments))
            {
                opt_error = cyFailureToOptError(*cy_failure);
                logger().warn("RawPublisherSvc: failed to publish raw message (err={}, fsm_id={})", opt_error, id_);
            }

            sendPublishResponse(opt_error);
        }

        bool makeCyPublisher(const sdk::CyphalPortId subject_id)
        {
            using CyMakeFailure = libcyphal::presentation::Presentation::MakeFailure;

            auto cy_make_result = service_.context_.presentation.makePublisher<void>(subject_id);
            if (const auto* const cy_failure = cetl::get_if<CyMakeFailure>(&cy_make_result))
            {
                const auto opt_error = cyFailureToOptError(*cy_failure);
                logger().warn("RawPublisherSvc: failed to make publisher (subj_id={}, err={}, fsm_id={}).",
                              subject_id,
                              opt_error,
                              id_);

                complete(opt_error);
                return false;
            }

            cy_raw_publisher_.emplace(cetl::get<CyRawPublisher>(std::move(cy_make_result)));
            return true;
        }

        void sendPublishResponse(const sdk::OptError opt_error = {})
        {
            Spec::Response ipc_response{&memory()};
            auto&          publish_error = ipc_response.set_publish_error();
            optErrorToDsdlError(opt_error, publish_error);

            if (const auto send_opt_error = channel_.send(ipc_response))
            {
                logger().warn("RawPublisherSvc: failed to send ipc response (err={}, fsm_id={}).",
                              *send_opt_error,
                              id_);
            }
        }

        void complete(const sdk::OptError completion_opt_error = {})
        {
            cy_raw_publisher_.reset();

            if (const auto opt_error = channel_.complete(completion_opt_error))
            {
                logger().warn("RawPublisherSvc: failed to complete channel (err={}, fsm_id={}).", *opt_error, id_);
            }

            service_.releaseFsmBy(id_);
        }

        static libcyphal::transport::Priority convertToCyPriority(const std::uint8_t raw_priority)
        {
            return static_cast<libcyphal::transport::Priority>(raw_priority);
        }

        const Id                       id_;
        Channel                        channel_;
        RawPublisherServiceImpl&       service_;
        cetl::optional<CyRawPublisher> cy_raw_publisher_;

    };  // Fsm

    void releaseFsmBy(const Fsm::Id fsm_id)
    {
        id_to_fsm_.erase(fsm_id);
    }

    const ScvContext                      context_;
    std::uint64_t                         next_fsm_id_{0};
    std::unordered_map<Fsm::Id, Fsm::Ptr> id_to_fsm_;
    common::LoggerPtr                     logger_{common::getLogger("engine")};

};  // RawPublisherServiceImpl

}  // namespace

void RawPublisherService::registerWithContext(const ScvContext& context)
{
    using Impl = RawPublisherServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context});
}

}  // namespace relay
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
