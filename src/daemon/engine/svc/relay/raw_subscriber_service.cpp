//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "raw_subscriber_service.hpp"

#include "engine_helpers.hpp"
#include "io/socket_buffer.hpp"
#include "logging.hpp"
#include "svc/relay/raw_subscriber_spec.hpp"
#include "svc/svc_helpers.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/presentation/subscriber.hpp>

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

/// Defines 'Relay: Raw Subscriber' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class RawSubscriberServiceImpl final
{
public:
    using Spec    = common::svc::relay::RawSubscriberSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit RawSubscriberServiceImpl(const ScvContext& context)
        : context_{context}
    {
    }

    /// Handles the initial `relay::RawSubscriber` service request of a new IPC channel.
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

        Fsm(RawSubscriberServiceImpl& service, const Id id, Channel&& channel)
            : id_{id}
            , channel_{std::move(channel)}
            , service_{service}
        {
            logger().trace("RawSubscriberSvc::Fsm (id={}).", id_);

            channel_.subscribe([this](const auto& event_var, const auto&) {
                //
                cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
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
                if (makeCySubscriber(create_req->subject_id, create_req->extent_size))
                {
                    const Spec::Response ipc_response{&memory()};
                    if (const auto opt_error = channel_.send(ipc_response))
                    {
                        logger().warn("RawSubscriberSvc: failed to send ipc reply (err={}, fsm_id={}).",
                                      *opt_error,
                                      id_);
                        complete(opt_error);
                    }
                }
            }
        }

    private:
        using CyScatteredBuff = libcyphal::transport::ScatteredBuffer;
        using CyMsgRxMetadata = libcyphal::transport::MessageRxMetadata;
        using CyRawSubscriber = libcyphal::presentation::Subscriber<void>;

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
            logger().debug("RawSubscriberSvc::handleEvent({}) (fsm_id={}).", completed, id_);

            if (!completed.keep_alive)
            {
                logger().warn("RawSubscriberSvc: canceling processing (fsm_id={}).", id_);
                complete(sdk::Error{sdk::Error::Code::Canceled});
            }
        }

        bool makeCySubscriber(const sdk::CyphalPortId subject_id, const std::size_t extent_bytes)
        {
            using CyMakeFailure = libcyphal::presentation::Presentation::MakeFailure;

            auto cy_make_result = service_.context_.presentation.makeSubscriber(  //
                subject_id,
                extent_bytes,
                [this](const auto& arg) {
                    //
                    handleNodeMessage(arg.raw_message, arg.metadata);
                });
            if (const auto* const cy_failure = cetl::get_if<CyMakeFailure>(&cy_make_result))
            {
                const auto opt_error = cyFailureToOptError(*cy_failure);
                logger().warn("RawSubscriberSvc: failed to make subscriber (subj_id={}, err={}, fsm_id={}).",
                              subject_id,
                              opt_error,
                              id_);

                complete(opt_error);
                return false;
            }

            cy_raw_subscriber_.emplace(cetl::get<CyRawSubscriber>(std::move(cy_make_result)));
            return true;
        }

        void handleNodeMessage(const CyScatteredBuff& raw_msg_buff, const CyMsgRxMetadata& metadata)
        {
            Spec::Response ipc_response{&memory()};
            auto&          raw_sub_msg = ipc_response.set_receive();
            raw_sub_msg.priority       = static_cast<std::uint8_t>(metadata.rx_meta.base.priority);
            raw_sub_msg.payload_size   = raw_msg_buff.size();
            if (const auto opt_node_id = metadata.publisher_node_id)
            {
                raw_sub_msg.remote_node_id.push_back(*opt_node_id);
            }

            common::io::SocketBuffer sock_buff{raw_msg_buff};
            if (const auto opt_error = channel_.send(ipc_response, sock_buff))
            {
                logger().warn("RawSubscriberSvc: failed to send ipc response (err={}, fsm_id={}).", *opt_error, id_);
            }
        }

        void complete(const sdk::OptError completion_opt_error = {})
        {
            cy_raw_subscriber_.reset();

            if (const auto opt_error = channel_.complete(completion_opt_error))
            {
                logger().warn("RawSubscriberSvc: failed to complete channel (err={}, fsm_id={}).", *opt_error, id_);
            }

            service_.releaseFsmBy(id_);
        }

        const Id                        id_;
        Channel                         channel_;
        RawSubscriberServiceImpl&       service_;
        cetl::optional<CyRawSubscriber> cy_raw_subscriber_;

    };  // Fsm

    void releaseFsmBy(const Fsm::Id fsm_id)
    {
        id_to_fsm_.erase(fsm_id);
    }

    const ScvContext                      context_;
    std::uint64_t                         next_fsm_id_{0};
    std::unordered_map<Fsm::Id, Fsm::Ptr> id_to_fsm_;
    common::LoggerPtr                     logger_{common::getLogger("engine")};

};  // RawSubscriberServiceImpl

}  // namespace

void RawSubscriberService::registerWithContext(const ScvContext& context)
{
    using Impl = RawSubscriberServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context});
}

}  // namespace relay
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
