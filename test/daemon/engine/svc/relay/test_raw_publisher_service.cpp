//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "svc/relay/raw_publisher_service.hpp"

#include "common/io/io_gtest_helpers.hpp"
#include "common/ipc/gateway_mock.hpp"
#include "common/ipc/server_router_mock.hpp"
#include "daemon/engine/cyphal/msg_sessions_mock.hpp"
#include "daemon/engine/cyphal/transport_gtest_helpers.hpp"
#include "daemon/engine/cyphal/transport_mock.hpp"
#include "ipc/channel.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "svc/relay/raw_publisher_spec.hpp"
#include "svc/svc_helpers.hpp"
#include "tracking_memory_resource.hpp"
#include "verify_utilz.hpp"
#include "virtual_time_scheduler.hpp"

#include <ocvsmd/common/svc/relay/RawPublisherPublish_0_1.hpp>
#include <uavcan/node/Version_1_0.hpp>

#include <cetl/pf17/cetlpf.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace
{

using namespace ocvsmd::common;               // NOLINT This our main concern here in the unit tests.
using namespace ocvsmd::daemon::engine::svc;  // NOLINT This our main concern here in the unit tests.
using ocvsmd::sdk::Error;
using ocvsmd::sdk::OptError;

using ocvsmd::verify_utilz::b;

using testing::_;
using testing::Invoke;
using testing::IsNull;
using testing::Return;
using testing::IsEmpty;
using testing::NotNull;
using testing::StrictMock;
using testing::VariantWith;

// https://github.com/llvm/llvm-project/issues/53444
// NOLINTBEGIN(misc-unused-using-decls, misc-include-cleaner)
using std::literals::chrono_literals::operator""s;
using std::literals::chrono_literals::operator""ms;
using std::literals::chrono_literals::operator""us;
// NOLINTEND(misc-unused-using-decls, misc-include-cleaner)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

class TestRawPublisherService : public testing::Test
{
protected:
    using Spec          = svc::relay::RawPublisherSpec;
    using GatewayMock   = ipc::detail::GatewayMock;
    using GatewayEvent  = ipc::detail::Gateway::Event;
    using ErrorResponse = Error_0_1;
    using EmptyResponse = uavcan::primitive::Empty_1_0;

    using CyPortId             = libcyphal::transport::PortId;
    using CyPriority           = libcyphal::transport::Priority;
    using CyPresentation       = libcyphal::presentation::Presentation;
    using CyProtocolParams     = libcyphal::transport::ProtocolParams;
    using CyMsgTxSessionMock   = StrictMock<libcyphal::transport::MessageTxSessionMock>;
    using CyUniquePtrMsgTxSpec = CyMsgTxSessionMock::RefWrapper::Spec;

    struct CySessCntx
    {
        CyMsgTxSessionMock msg_tx_mock;
    };

    void SetUp() override
    {
        cetl::pmr::set_default_resource(&mr_);

        EXPECT_CALL(cy_transport_mock_, getProtocolParams())
            .WillRepeatedly(
                Return(CyProtocolParams{std::numeric_limits<libcyphal::transport::TransferId>::max(), 0, 0}));
    }

    void TearDown() override
    {
        EXPECT_THAT(mr_.allocations, IsEmpty());
        EXPECT_THAT(mr_.total_allocated_bytes, mr_.total_deallocated_bytes);
    }

    libcyphal::TimePoint now() const
    {
        return scheduler_.now();
    }

    void expectCyMsgSession(CySessCntx& cy_sess_cntx, const CyPortId subject_id)
    {
        const libcyphal::transport::MessageTxParams tx_params{subject_id};

        EXPECT_CALL(cy_sess_cntx.msg_tx_mock, getParams())  //
            .WillOnce(Return(tx_params));
        EXPECT_CALL(cy_transport_mock_, makeMessageTxSession(MessageTxParamsEq(tx_params)))  //
            .WillOnce(Invoke([&](const auto&) {                                              //
                return libcyphal::detail::makeUniquePtr<CyUniquePtrMsgTxSpec>(mr_, cy_sess_cntx.msg_tx_mock);
            }));
        EXPECT_CALL(cy_sess_cntx.msg_tx_mock, deinit()).Times(1);
    }

    // NOLINTBEGIN
    ocvsmd::TrackingMemoryResource                  mr_;
    ocvsmd::VirtualTimeScheduler                    scheduler_{};
    StrictMock<libcyphal::transport::TransportMock> cy_transport_mock_;
    StrictMock<ipc::ServerRouterMock>               ipc_router_mock_{mr_};
    const std::string                               svc_name_{Spec::svc_full_name()};
    const ipc::detail::ServiceDesc svc_desc_{ipc::AnyChannel::getServiceDesc<Spec::Request>(svc_name_)};

    // NOLINTEND
};

// MARK: - Tests:

TEST_F(TestRawPublisherService, registerWithContext)
{
    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_THAT(ipc_router_mock_.getChannelFactory(svc_desc_), IsNull());

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(svc_name_)).WillOnce(Return());
    relay::RawPublisherService::registerWithContext(svc_context);

    EXPECT_THAT(ipc_router_mock_.getChannelFactory(svc_desc_), NotNull());
}

TEST_F(TestRawPublisherService, request_config)
{
    using libcyphal::transport::TransferTxMetadataEq;

    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(_)).WillOnce(Return());
    relay::RawPublisherService::registerWithContext(svc_context);

    auto* const ch_factory = ipc_router_mock_.getChannelFactory(svc_desc_);
    ASSERT_THAT(ch_factory, NotNull());

    StrictMock<GatewayMock> gateway_mock;

    Spec::Request request{&mr_};
    auto&         create_req = request.set_create();
    create_req.subject_id    = 123;

    const auto expected_empty    = VariantWith<EmptyResponse>(_);
    const auto expected_no_error = VariantWith<ErrorResponse>(ErrorResponse{0, 0, &mr_});

    CySessCntx cy_sess_cntx;

    scheduler_.scheduleAt(1s, [&](const auto&) {
        //
        // Emulate service 'create' request.
        expectCyMsgSession(cy_sess_cntx, create_req.subject_id);
        EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Response>(mr_, expected_empty)))
            .WillOnce(Return(OptError{}));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            (*ch_factory)(std::make_shared<GatewayMock::Wrapper>(gateway_mock), payload);
            return OptError{};
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(2s, [&](const auto&) {
        //
        // Emulate service 'config' request.
        auto& config = request.set_config();
        config.priority.push_back(static_cast<std::uint8_t>(CyPriority::High));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            return gateway_mock.event_handler_(GatewayEvent::Message{1, payload});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(3s, [&](const auto&) {
        //
        // Emulate service 'empty config' request - should keep previously set 'High' priority.
        auto& config = request.set_config();
        config.priority.clear();
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            return gateway_mock.event_handler_(GatewayEvent::Message{2, payload});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(4s, [&](const auto&) {
        //
        // Emulate service 'publish' request with empty payload.
        auto& publish      = request.set_publish();
        publish.timeout_us = 345;
        EXPECT_CALL(cy_sess_cntx.msg_tx_mock, send(TransferTxMetadataEq({{0, CyPriority::High}, now() + 345us}), _))
            .WillOnce(Return(cetl::nullopt));
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Response>(mr_, expected_no_error)))
            .WillOnce(Return(OptError{}));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            return gateway_mock.event_handler_(GatewayEvent::Message{3, payload});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(9s, [&](const auto&) {
        //
        EXPECT_CALL(gateway_mock, complete(OptError{}, false)).WillOnce(Return(OptError{}));
        EXPECT_CALL(gateway_mock, deinit()).Times(1);
        gateway_mock.event_handler_(GatewayEvent::Completed{OptError{}, false});
    });
    scheduler_.scheduleAt(9s + 1ms, [&](const auto&) {
        //
        testing::Mock::VerifyAndClearExpectations(&gateway_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_cntx.msg_tx_mock);
    });
    scheduler_.spinFor(10s);
}

TEST_F(TestRawPublisherService, request_publish)
{
    using libcyphal::transport::TransferTxMetadataEq;

    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(_)).WillOnce(Return());
    relay::RawPublisherService::registerWithContext(svc_context);

    auto* const ch_factory = ipc_router_mock_.getChannelFactory(svc_desc_);
    ASSERT_THAT(ch_factory, NotNull());

    StrictMock<GatewayMock> gateway_mock;

    Spec::Request request{&mr_};
    auto&         create_req = request.set_create();
    create_req.subject_id    = 123;

    std::array<cetl::byte, 3> test_raw_bytes{b(0x11), b(0x22), b(0x33)};

    const auto expected_empty    = VariantWith<EmptyResponse>(_);
    const auto expected_no_error = VariantWith<ErrorResponse>(ErrorResponse{0, 0, &mr_});

    CySessCntx cy_sess_cntx;

    scheduler_.scheduleAt(1s, [&](const auto&) {
        //
        // Emulate service 'create' request.
        expectCyMsgSession(cy_sess_cntx, create_req.subject_id);
        EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Response>(mr_, expected_empty)))
            .WillOnce(Return(OptError{}));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            (*ch_factory)(std::make_shared<GatewayMock::Wrapper>(gateway_mock), payload);
            return OptError{};
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(2s, [&](const auto&) {
        //
        // Emulate service 'publish' request with 3-bytes payload.
        auto& publish        = request.set_publish();
        publish.timeout_us   = 1'000'000;
        publish.payload_size = test_raw_bytes.size();
        EXPECT_CALL(cy_sess_cntx.msg_tx_mock, send(TransferTxMetadataEq({{0, CyPriority::Nominal}, now() + 1s}), _))
            .WillOnce(Return(cetl::nullopt));
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Response>(mr_, expected_no_error)))
            .WillOnce(Return(OptError{}));
        const auto result = tryPerformOnSerialized(request, [&](const auto req_payload) {
            //
            const auto size = req_payload.size() + test_raw_bytes.size();
            auto       data = std::make_unique<cetl::byte[]>(size);  // NOLINT(*-avoid-c-arrays)
            std::copy(req_payload.begin(), req_payload.end(), data.get());
            std::copy(test_raw_bytes.begin(), test_raw_bytes.end(), data.get() + req_payload.size());
            return gateway_mock.event_handler_(GatewayEvent::Message{1, {data.get(), size}});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(3s, [&](const auto&) {
        //
        // Emulate service 'publish' request, which will fail at the Cyphal transport.
        auto& publish        = request.set_publish();
        publish.timeout_us   = 1'000'000;
        publish.payload_size = 0;
        EXPECT_CALL(cy_sess_cntx.msg_tx_mock, send(TransferTxMetadataEq({{1, CyPriority::Nominal}, now() + 1s}), _))
            .WillOnce(Return(libcyphal::transport::CapacityError{}));
        const auto expected_oom_error = VariantWith<ErrorResponse>(  //
            ErrorResponse{static_cast<std::uint32_t>(Error::Code::OutOfMemory), 0, &mr_});
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Response>(mr_, expected_oom_error)))
            .WillOnce(Return(OptError{}));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            return gateway_mock.event_handler_(GatewayEvent::Message{2, payload});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(9s, [&](const auto&) {
        //
        EXPECT_CALL(gateway_mock, complete(OptError{}, false)).WillOnce(Return(OptError{}));
        EXPECT_CALL(gateway_mock, deinit()).Times(1);
        gateway_mock.event_handler_(GatewayEvent::Completed{OptError{}, false});
    });
    scheduler_.scheduleAt(9s + 1ms, [&](const auto&) {
        //
        testing::Mock::VerifyAndClearExpectations(&gateway_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_cntx.msg_tx_mock);
    });
    scheduler_.spinFor(10s);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

}  // namespace
