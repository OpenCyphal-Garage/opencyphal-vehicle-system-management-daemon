//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "raw_publisher_client.hpp"

#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/execution.hpp"
#include "ocvsmd/sdk/node_pub_sub.hpp"
#include "svc/as_sender.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/visit_helpers.hpp>

#include <memory>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace relay
{
namespace
{

class RawPublisherClientImpl final : public RawPublisherClient
{
public:
    RawPublisherClientImpl(cetl::pmr::memory_resource&           memory,
                           const common::ipc::ClientRouter::Ptr& ipc_router,
                           Spec::Request                         request)
        : memory_{memory}
        , logger_{common::getLogger("svc")}
        , request_{std::move(request)}
        , channel_{ipc_router->makeChannel<Channel>(Spec::svc_full_name())}
    {
    }

    void submitImpl(std::function<void(Result&&)>&& receiver) override
    {
        receiver_ = std::move(receiver);

        channel_.subscribe([this](const auto& event_var, const auto) {
            //
            cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
        });
    }

private:
    using Channel = common::ipc::Channel<Spec::Response, Spec::Request>;

    class RawPublisherImpl final : public std::enable_shared_from_this<RawPublisherImpl>, public RawPublisher
    {
    public:
        RawPublisherImpl(cetl::pmr::memory_resource& memory, common::LoggerPtr logger, Channel&& channel)
            : memory_{memory}
            , logger_(std::move(logger))
            , channel_{std::move(channel)}
            , published_{PublishRequest{&memory}, {}}
        {
            channel_.subscribe([this](const auto& event_var, const auto) {
                //
                cetl::visit(                //
                    cetl::make_overloaded(  //
                        [this](const Channel::Input& input) {
                            //
                            handleEvent(input);
                        },
                        [this](const Channel::Completed& completed) {
                            //
                            handleEvent(completed);
                        },
                        [this](const Channel::Connected&) {}),
                    event_var);
            });
        }

        template <typename Receiver>
        void submit(Receiver&& receiver)
        {
            if (const auto error = completion_error_)
            {
                logger_->warn("RawPublisher::submit() Already completed with error (err={}).", *error);
                receiver(OptError{error});
                return;
            }

            Spec::Request request{&memory_};
            auto&         publish = request.set_publish(published_.request);
            publish.payload_size  = published_.payload.size;
            SocketBuffer sock_buff{{published_.payload.data.get(), publish.payload_size}};
            const auto   opt_send_error = channel_.send(request, sock_buff);

            // Raw message payload has been sent, so no need to keep it in memory.
            published_.payload.size = 0;
            published_.payload.data.reset();

            if (const auto error = opt_send_error)
            {
                logger_->warn("RawPublisher::submit() Failed to send 'publish' request (err={}).", *error);
                receiver(OptError{error});
                return;
            }

            receiver_ = std::forward<Receiver>(receiver);
        }

        // RawPublisher

        SenderOf<OptError>::Ptr publish(OwnMutablePayload&& raw_msg, const std::chrono::microseconds timeout) override
        {
            published_.payload            = std::move(raw_msg);
            published_.request.timeout_us = std::max<std::uint64_t>(0, timeout.count());

            return std::make_unique<AsSender<OptError, decltype(shared_from_this())>>(  //
                "RawPublisher::publish",
                shared_from_this(),
                logger_);
        }

        OptError setPriority(const CyphalPriority priority) override
        {
            if (const auto error = completion_error_)
            {
                logger_->warn("RawPublisher::setPriority() Already completed with error (err={}).", *error);
                return error;
            }

            Spec::Request request{&memory_};
            auto&         config = request.set_config();
            config.priority.push_back(static_cast<std::uint8_t>(priority));

            const auto opt_error = channel_.send(request);
            if (opt_error)
            {
                logger_->warn("RawPublisher::setPriority() Failed to send 'config' request (err={}).", *opt_error);
            }
            return opt_error;
        }

    private:
        using SocketBuffer    = common::io::SocketBuffer;
        using PublishRequest  = Spec::Request::_traits_::TypeOf::publish;
        using PublishResponse = Spec::Response::_traits_::TypeOf::publish_error;

        struct Published
        {
            PublishRequest    request;
            OwnMutablePayload payload;  // NOLINT(*-avoid-c-arrays)
        };

        void handleEvent(const Channel::Input& input)
        {
            logger_->trace("RawPublisher::handleEvent(Input).");

            cetl::visit(                //
                cetl::make_overloaded(  //
                    [this](const auto& published) {
                        //
                        handleInputEvent(published);
                    },
                    [](const uavcan::primitive::Empty_1_0&) {}),
                input.union_value);
        }

        void handleEvent(const Channel::Completed& completed)
        {
            logger_->debug("RawPublisher::handleEvent({}).", completed);
            completion_error_ = completed.opt_error.value_or(Error{Error::Code::Canceled});
            notifyPublished(completion_error_);
        }

        void handleInputEvent(const PublishResponse& publish_error) const
        {
            notifyPublished(dsdlErrorToOptError(publish_error));
        }

        void notifyPublished(const OptError opt_error) const
        {
            if (receiver_)
            {
                receiver_(opt_error);
            }
        }

        cetl::pmr::memory_resource&   memory_;
        common::LoggerPtr             logger_;
        Channel                       channel_;
        Published                     published_;
        OptError                      completion_error_;
        std::function<void(OptError)> receiver_;

    };  // RawPublisherImpl

    void handleEvent(const Channel::Connected& connected)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->trace("RawPublisherClient::handleEvent({}).", connected);

        if (const auto opt_error = channel_.send(request_))
        {
            logger_->warn("RawPublisherClient::handleEvent() Failed to send request (err={}).", *opt_error);
            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input&)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->trace("RawPublisherClient::handleEvent(Input).");

        auto raw_publisher = std::make_shared<RawPublisherImpl>(memory_, logger_, std::move(channel_));
        receiver_(Success{std::move(raw_publisher)});
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->debug("RawPublisherClient::handleEvent({}).", completed);

        receiver_(Failure{completed.opt_error.value_or(Error{Error::Code::Canceled})});
    }

    cetl::pmr::memory_resource&   memory_;
    common::LoggerPtr             logger_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;

};  // RawPublisherClientImpl

}  // namespace

RawPublisherClient::Ptr RawPublisherClient::make(  //
    cetl::pmr::memory_resource&           memory,
    const common::ipc::ClientRouter::Ptr& ipc_router,
    const Spec::Request&                  request)
{
    return std::make_shared<RawPublisherClientImpl>(memory, ipc_router, request);
}

}  // namespace relay
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
