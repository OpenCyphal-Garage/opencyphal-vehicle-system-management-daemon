//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "svc/node/exec_cmd_service.hpp"

#include "common/ipc/gateway_mock.hpp"
#include "common/ipc/ipc_gtest_helpers.hpp"
#include "common/ipc/server_router_mock.hpp"
#include "daemon/engine/cyphal/svc_sessions_mock.hpp"
#include "daemon/engine/cyphal/transport_gtest_helpers.hpp"
#include "daemon/engine/cyphal/transport_mock.hpp"
#include "ipc/channel.hpp"
#include "svc/node/exec_cmd_spec.hpp"
#include "svc/node/services.hpp"
#include "svc/svc_helpers.hpp"
#include "tracking_memory_resource.hpp"
#include "virtual_time_scheduler.hpp"

#include <libcyphal/errors.hpp>
#include <uavcan/node/ExecuteCommand_1_3.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace
{

using namespace ocvsmd::common;               // NOLINT This our main concern here in the unit tests.
using namespace ocvsmd::daemon::engine::svc;  // NOLINT This our main concern here in the unit tests.

using testing::_;
using testing::Invoke;
using testing::IsNull;
using testing::Return;
using testing::IsEmpty;
using testing::NotNull;
using testing::StrictMock;

// https://github.com/llvm/llvm-project/issues/53444
// NOLINTBEGIN(misc-unused-using-decls, misc-include-cleaner)
using std::literals::chrono_literals::operator""s;
using std::literals::chrono_literals::operator""ms;
// NOLINTEND(misc-unused-using-decls, misc-include-cleaner)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

class TestExecCmdService : public testing::Test
{
protected:
    using ExecCmdSpec = svc::node::ExecCmdSpec;
    using GatewayMock = ipc::detail::GatewayMock;

    using CyService               = uavcan::node::ExecuteCommand_1_3;
    using CyPresentation          = libcyphal::presentation::Presentation;
    using CyProtocolParams        = libcyphal::transport::ProtocolParams;
    using CyServiceRxTransfer     = libcyphal::transport::ServiceRxTransfer;
    using CyRequestTxSessionMock  = StrictMock<libcyphal::transport::RequestTxSessionMock>;
    using CyResponseRxSessionMock = StrictMock<libcyphal::transport::ResponseRxSessionMock>;
    using CyUniquePtrReqTxSpec    = CyRequestTxSessionMock::RefWrapper::Spec;
    using CyUniquePtrResRxSpec    = CyResponseRxSessionMock::RefWrapper::Spec;
    struct CySvcSessions
    {
        CyRequestTxSessionMock                               req_tx_mock;
        CyResponseRxSessionMock                              res_rx_mock;
        CyResponseRxSessionMock::OnReceiveCallback::Function res_rx_cb_fn;
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

    void expectCySvcSessions(CySvcSessions& cy_sess_mocks, const libcyphal::transport::NodeId node_id)
    {
        const libcyphal::transport::RequestTxParams  tx_params{CyService::Request::_traits_::FixedPortId, node_id};
        const libcyphal::transport::ResponseRxParams rx_params{CyService::Response::_traits_::ExtentBytes,
                                                               tx_params.service_id,
                                                               tx_params.server_node_id};

        EXPECT_CALL(cy_transport_mock_, makeRequestTxSession(RequestTxParamsEq(tx_params)))  //
            .WillOnce(Invoke([&](const auto&) {                                              //
                return libcyphal::detail::makeUniquePtr<CyUniquePtrReqTxSpec>(mr_, cy_sess_mocks.req_tx_mock);
            }));
        EXPECT_CALL(cy_sess_mocks.req_tx_mock, deinit()).Times(1);

        EXPECT_CALL(cy_sess_mocks.res_rx_mock, getParams())  //
            .WillOnce(Return(rx_params));
        EXPECT_CALL(cy_sess_mocks.res_rx_mock, setTransferIdTimeout(_))  //
            .WillOnce(Return());
        EXPECT_CALL(cy_sess_mocks.res_rx_mock, setOnReceiveCallback(_))  //
            .WillRepeatedly(Invoke([&](auto&& cb_fn) {                   //
                cy_sess_mocks.res_rx_cb_fn = std::forward<decltype(cb_fn)>(cb_fn);
            }));
        EXPECT_CALL(cy_transport_mock_, makeResponseRxSession(ResponseRxParamsEq(rx_params)))  //
            .WillOnce(Invoke([&](const auto&) {                                                //
                return libcyphal::detail::makeUniquePtr<CyUniquePtrResRxSpec>(mr_, cy_sess_mocks.res_rx_mock);
            }));
        EXPECT_CALL(cy_sess_mocks.res_rx_mock, deinit()).Times(1);
    }

    // NOLINTBEGIN
    ocvsmd::TrackingMemoryResource                  mr_;
    ocvsmd::VirtualTimeScheduler                    scheduler_{};
    StrictMock<libcyphal::transport::TransportMock> cy_transport_mock_;
    StrictMock<ipc::ServerRouterMock>               ipc_router_mock_{mr_};
    const std::string                               svc_name_{ExecCmdSpec::svc_full_name()};
    const ipc::detail::ServiceDesc svc_desc_{ipc::AnyChannel::getServiceDesc<ExecCmdSpec::Request>(svc_name_)};

    // NOLINTEND
};

// MARK: - Tests:

TEST_F(TestExecCmdService, registerWithContext)
{
    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_THAT(ipc_router_mock_.getChannelFactory(svc_desc_), IsNull());

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(svc_name_)).WillOnce(Return());
    node::ExecCmdService::registerWithContext(svc_context);

    EXPECT_THAT(ipc_router_mock_.getChannelFactory(svc_desc_), NotNull());
}

TEST_F(TestExecCmdService, empty_request)
{
    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(_)).WillOnce(Return());
    node::ExecCmdService::registerWithContext(svc_context);

    auto* const ch_factory = ipc_router_mock_.getChannelFactory(svc_desc_);
    ASSERT_THAT(ch_factory, NotNull());

    {
        StrictMock<GatewayMock> gateway_mock;
        auto                    gateway = std::make_shared<GatewayMock::Wrapper>(gateway_mock);

        const ExecCmdSpec::Request request{&mr_};

        EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
        EXPECT_CALL(gateway_mock, complete(0, false)).Times(1);
        EXPECT_CALL(gateway_mock, deinit()).Times(1);
        //
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            (*ch_factory)(std::move(gateway), payload);
            return 0;
        });
        EXPECT_THAT(result, 0);
    }
}

TEST_F(TestExecCmdService, two_nodes_request)
{
    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(_)).WillOnce(Return());
    node::ExecCmdService::registerWithContext(svc_context);

    auto* const ch_factory = ipc_router_mock_.getChannelFactory(svc_desc_);
    ASSERT_THAT(ch_factory, NotNull());

    StrictMock<GatewayMock> gateway_mock;

    ExecCmdSpec::Request request{&mr_};
    request.timeout_us = 1'000'000;
    request.node_ids.push_back(42);
    request.node_ids.push_back(43);
    request.node_ids.push_back(42);  // Duplicate node ID.

    CySvcSessions cy_sess_42;
    CySvcSessions cy_sess_43;
    EXPECT_CALL(cy_sess_42.req_tx_mock, send(_, _)).WillOnce(Return(cetl::nullopt));
    EXPECT_CALL(cy_sess_43.req_tx_mock, send(_, _)).WillOnce(Return(cetl::nullopt));

    scheduler_.scheduleAt(1s, [&](const auto&) {
        //
        // Emulate service request.
        EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
        expectCySvcSessions(cy_sess_42, 42);
        expectCySvcSessions(cy_sess_43, 43);
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            (*ch_factory)(std::make_shared<GatewayMock::Wrapper>(gateway_mock), payload);
            return 0;
        });
        EXPECT_THAT(result, 0);
    });
    scheduler_.scheduleAt(1s + 100ms, [&](const auto&) {
        //
        // Emulate that node 42 has responded in time (after 100ms).
        ExecCmdSpec::Response expected_response{&mr_};
        expected_response.node_id = 42;
        EXPECT_CALL(gateway_mock, send(_, ipc::PayloadWith<ExecCmdSpec::Response>(mr_, expected_response))).Times(1);
        CyServiceRxTransfer transfer{{{{0, libcyphal::transport::Priority::Nominal}, now()}, 42}, {}};
        cy_sess_42.res_rx_cb_fn({transfer});
    });
    scheduler_.scheduleAt(2s, [&](const auto&) {
        //
        EXPECT_CALL(gateway_mock, complete(0, false)).Times(1);
        EXPECT_CALL(gateway_mock, deinit()).Times(1);
    });
    scheduler_.scheduleAt(2s + 1ms, [&](const auto&) {
        //
        testing::Mock::VerifyAndClearExpectations(&gateway_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_42.req_tx_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_42.res_rx_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_43.req_tx_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_43.res_rx_mock);
    });
    scheduler_.spinFor(10s);
}

TEST_F(TestExecCmdService, out_of_memory)
{
    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(_)).WillOnce(Return());
    node::ExecCmdService::registerWithContext(svc_context);

    auto* const ch_factory = ipc_router_mock_.getChannelFactory(svc_desc_);
    ASSERT_THAT(ch_factory, NotNull());

    {
        StrictMock<GatewayMock> gateway_mock;
        auto                    gateway = std::make_shared<GatewayMock::Wrapper>(gateway_mock);

        ExecCmdSpec::Request request{&mr_};
        request.node_ids.push_back(13);
        request.node_ids.push_back(31);

        EXPECT_CALL(cy_transport_mock_, makeRequestTxSession(_)).WillOnce(Return(libcyphal::MemoryError{}));

        EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
        EXPECT_CALL(gateway_mock, complete(ENOMEM, false)).Times(1);
        EXPECT_CALL(gateway_mock, deinit()).Times(1);
        //
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            (*ch_factory)(std::move(gateway), payload);
            return 0;
        });
        EXPECT_THAT(result, 0);
    }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

}  // namespace

namespace ocvsmd
{
namespace common
{
namespace svc
{
namespace node
{
namespace ExecCmd
{
static void PrintTo(const Response_0_1& res, std::ostream* os)  // NOLINT
{
    *os << "ExecCmd::Response_0_1{node_id=" << res.node_id << "}";
}
static bool operator==(const Response_0_1& lhs, const Response_0_1& rhs)  // NOLINT
{
    return lhs.node_id == rhs.node_id;
}
}  // namespace ExecCmd
}  // namespace node
}  // namespace svc
}  // namespace common
}  // namespace ocvsmd
