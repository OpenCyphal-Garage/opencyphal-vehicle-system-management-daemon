//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "ipc/client_router.hpp"

#include "cetl_gtest_helpers.hpp"  // NOLINT(misc-include-cleaner)
#include "ipc/channel.hpp"
#include "ipc/ipc_types.hpp"
#include "ipc/pipe/client_pipe.hpp"
#include "ipc_gtest_helpers.hpp"
#include "pipe/client_pipe_mock.hpp"
#include "tracking_memory_resource.hpp"

#include "ocvsmd/common/ipc/RouteChannelMsg_0_1.hpp"
#include "ocvsmd/common/ipc/RouteConnect_0_1.hpp"
#include "ocvsmd/common/ipc/Route_0_2.hpp"
#include "ocvsmd/common/svc/node/ExecCmd_0_1.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <iterator>
#include <memory>
#include <vector>

namespace
{

using namespace ocvsmd::common::ipc;  // NOLINT This our main concern here in the unit tests.

using testing::_;
using testing::IsTrue;
using testing::Return;
using testing::IsEmpty;
using testing::IsFalse;
using testing::NotNull;
using testing::StrictMock;
using testing::VariantWith;
using testing::MockFunction;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, bugprone-unchecked-optional-access)

class TestClientRouter : public testing::Test
{
protected:
    void SetUp() override
    {
        cetl::pmr::set_default_resource(&mr_);
    }

    void TearDown() override
    {
        EXPECT_THAT(mr_.allocations, IsEmpty());
        EXPECT_THAT(mr_.total_allocated_bytes, mr_.total_deallocated_bytes);
    }

    void emulateRouteConnect(pipe::ClientPipeMock& client_pipe_mock,
                             const std::uint8_t    ver_major  = VERSION_MAJOR,  // NOLINT
                             const std::uint8_t    ver_minor  = VERSION_MINOR,
                             ErrorCode             error_code = ErrorCode::Success)
    {
        using ocvsmd::common::tryPerformOnSerialized;

        // client RouteConnect -> server
        EXPECT_CALL(client_pipe_mock, send(PayloadOfRouteConnect(mr_)))  //
            .WillOnce(Return(0));
        client_pipe_mock.event_handler_(pipe::ClientPipe::Event::Connected{});

        Route_0_2 route{&mr_};
        auto&     rt_conn     = route.set_connect();
        rt_conn.version.major = ver_major;
        rt_conn.version.minor = ver_minor;
        rt_conn.error_code    = static_cast<std::int32_t>(error_code);
        //
        const int result = tryPerformOnSerialized(route, [&](const auto payload) {
            //
            return client_pipe_mock.event_handler_(pipe::ClientPipe::Event::Message{payload});
        });
        EXPECT_THAT(result, 0);
    }

    template <typename Msg>
    void emulateRouteChannelMsg(pipe::ClientPipeMock&   client_pipe_mock,
                                const std::uint64_t     tag,
                                const Msg&              msg,
                                std::uint64_t&          seq,
                                const cetl::string_view service_name = "")
    {
        using ocvsmd::common::tryPerformOnSerialized;

        Route_0_2 route{&mr_};
        auto&     channel_msg  = route.set_channel_msg();
        channel_msg.tag        = tag;
        channel_msg.sequence   = seq++;
        channel_msg.service_id = AnyChannel::getServiceDesc<Msg>(service_name).id;

        const int result = tryPerformOnSerialized(route, [&](const auto prefix) {
            //
            return tryPerformOnSerialized(msg, [&](const auto suffix) {
                //
                std::vector<std::uint8_t> buffer;
                std::copy(prefix.begin(), prefix.end(), std::back_inserter(buffer));
                std::copy(suffix.begin(), suffix.end(), std::back_inserter(buffer));
                const Payload payload{buffer.data(), buffer.size()};
                return client_pipe_mock.event_handler_(pipe::ClientPipe::Event::Message{payload});
            });
        });
        EXPECT_THAT(result, 0);
    }

    void emulateRouteChannelEnd(pipe::ClientPipeMock& client_pipe_mock,
                                const std::uint64_t   tag,
                                const ErrorCode       error_code = ErrorCode::Success,
                                const bool            keep_alive = false)
    {
        using ocvsmd::common::tryPerformOnSerialized;

        Route_0_2 route{&mr_};
        auto&     channel_end  = route.set_channel_end();
        channel_end.tag        = tag;
        channel_end.error_code = static_cast<std::int32_t>(error_code);
        channel_end.keep_alive = keep_alive;

        const int result = tryPerformOnSerialized(route, [&](const auto payload) {
            //
            return client_pipe_mock.event_handler_(pipe::ClientPipe::Event::Message{payload});
        });
        EXPECT_THAT(result, 0);
    }

    // MARK: Data members:

    // NOLINTBEGIN
    ocvsmd::TrackingMemoryResource mr_;
    // NOLINTEND
};

// MARK: - Tests:

TEST_F(TestClientRouter, make)
{
    StrictMock<pipe::ClientPipeMock> client_pipe_mock;

    const auto client_router = ClientRouter::make(  //
        mr_,
        std::make_unique<pipe::ClientPipeMock::Wrapper>(client_pipe_mock));
    ASSERT_THAT(client_router, NotNull());
    EXPECT_THAT(client_pipe_mock.event_handler_, IsFalse());

    EXPECT_CALL(client_pipe_mock, deinit()).Times(1);
}

TEST_F(TestClientRouter, start)
{
    StrictMock<pipe::ClientPipeMock> client_pipe_mock;

    const auto client_router = ClientRouter::make(  //
        mr_,
        std::make_unique<pipe::ClientPipeMock::Wrapper>(client_pipe_mock));
    ASSERT_THAT(client_router, NotNull());
    EXPECT_THAT(client_pipe_mock.event_handler_, IsFalse());

    EXPECT_CALL(client_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(client_router->start(), 0);
    EXPECT_THAT(client_pipe_mock.event_handler_, IsTrue());

    EXPECT_CALL(client_pipe_mock, deinit()).Times(1);
}

TEST_F(TestClientRouter, makeChannel)
{
    using Msg     = ocvsmd::common::svc::node::ExecCmd::Request_0_1;
    using Channel = Channel<Msg, Msg>;

    StrictMock<pipe::ClientPipeMock> client_pipe_mock;
    EXPECT_CALL(client_pipe_mock, deinit()).Times(1);

    const auto client_router = ClientRouter::make(  //
        mr_,
        std::make_unique<pipe::ClientPipeMock::Wrapper>(client_pipe_mock));
    ASSERT_THAT(client_router, NotNull());

    EXPECT_CALL(client_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(client_router->start(), 0);

    const auto channel = client_router->makeChannel<Channel>();
    (void) channel;
}

TEST_F(TestClientRouter, channel_send)
{
    using Msg     = ocvsmd::common::svc::node::ExecCmd::Request_0_1;
    using Channel = Channel<Msg, Msg>;

    StrictMock<pipe::ClientPipeMock> client_pipe_mock;
    EXPECT_CALL(client_pipe_mock, deinit()).Times(1);

    const auto client_router = ClientRouter::make(  //
        mr_,
        std::make_unique<pipe::ClientPipeMock::Wrapper>(client_pipe_mock));
    ASSERT_THAT(client_router, NotNull());

    EXPECT_CALL(client_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(client_router->start(), 0);

    auto channel = client_router->makeChannel<Channel>();

    emulateRouteConnect(client_pipe_mock);

    constexpr std::uint64_t tag = 0;
    std::uint64_t           seq = 0;
    const Msg               msg{&mr_};
    EXPECT_CALL(client_pipe_mock, send(PayloadOfRouteChannelMsg(msg, mr_, tag, seq++)))  //
        .WillOnce(Return(0));
    EXPECT_THAT(channel.send(msg), 0);

    EXPECT_CALL(client_pipe_mock, send(PayloadOfRouteChannelMsg(msg, mr_, tag, seq++)))  //
        .WillOnce(Return(0));
    EXPECT_THAT(channel.send(msg), 0);

    EXPECT_CALL(client_pipe_mock, send(PayloadOfRouteChannelEnd(mr_, tag, ErrorCode::Success)))  //
        .WillOnce(Return(0));
}

TEST_F(TestClientRouter, channel_send_after_end)
{
    using Msg     = ocvsmd::common::svc::node::ExecCmd::Request_0_1;
    using Channel = Channel<Msg, Msg>;

    StrictMock<pipe::ClientPipeMock> client_pipe_mock;
    EXPECT_CALL(client_pipe_mock, deinit()).Times(1);

    const auto client_router = ClientRouter::make(  //
        mr_,
        std::make_unique<pipe::ClientPipeMock::Wrapper>(client_pipe_mock));
    ASSERT_THAT(client_router, NotNull());

    EXPECT_CALL(client_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(client_router->start(), 0);

    StrictMock<MockFunction<void(const Channel::EventVar&)>> ch_event_mock;

    auto channel = client_router->makeChannel<Channel>();
    channel.subscribe(ch_event_mock.AsStdFunction());

    const Msg msg{&mr_};
    EXPECT_THAT(channel.send(msg), static_cast<int>(ErrorCode::NotConnected));
    EXPECT_THAT(channel.complete(), static_cast<int>(ErrorCode::NotConnected));

    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Connected>(_))).Times(1);
    emulateRouteConnect(client_pipe_mock);

    // Emulate that server posted final `RouteChannelEnd(keep-alive)`.
    //
    constexpr std::uint64_t tag = 0;
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Completed>(_))).Times(1);
    emulateRouteChannelEnd(client_pipe_mock, tag, ErrorCode::Success, true);

    std::uint64_t seq = 0;
    EXPECT_CALL(client_pipe_mock, send(PayloadOfRouteChannelMsg(msg, mr_, tag, seq++)))  //
        .WillOnce(Return(0));
    EXPECT_THAT(channel.send(msg), 0);
    //
    EXPECT_CALL(client_pipe_mock, send(PayloadOfRouteChannelEnd(mr_, tag, ErrorCode::Success, true)))  //
        .WillOnce(Return(0));
    EXPECT_THAT(channel.complete(0, true), 0);

    // Emulate that server posted final `RouteChannelEnd(keep-alive)`.
    //
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Completed>(_))).Times(1);
    emulateRouteChannelEnd(client_pipe_mock, tag, ErrorCode::Success, false);

    EXPECT_THAT(channel.send(msg), static_cast<int>(ErrorCode::Shutdown));
    EXPECT_THAT(channel.complete(), static_cast<int>(ErrorCode::Shutdown));
}

TEST_F(TestClientRouter, channel_receive_events)
{
    using Msg     = ocvsmd::common::svc::node::ExecCmd::Request_0_1;
    using Channel = Channel<Msg, Msg>;

    StrictMock<pipe::ClientPipeMock> client_pipe_mock;
    EXPECT_CALL(client_pipe_mock, deinit()).Times(1);

    const auto client_router = ClientRouter::make(  //
        mr_,
        std::make_unique<pipe::ClientPipeMock::Wrapper>(client_pipe_mock));
    ASSERT_THAT(client_router, NotNull());

    EXPECT_CALL(client_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(client_router->start(), 0);

    StrictMock<MockFunction<void(const Channel::EventVar&)>> ch1_event_mock;
    StrictMock<MockFunction<void(const Channel::EventVar&)>> ch2_event_mock;

    auto channel1 = client_router->makeChannel<Channel>();
    channel1.subscribe(ch1_event_mock.AsStdFunction());

    auto channel2 = client_router->makeChannel<Channel>();

    EXPECT_CALL(ch1_event_mock, Call(VariantWith<Channel::Connected>(_))).Times(1);
    emulateRouteConnect(client_pipe_mock);

    EXPECT_CALL(ch2_event_mock, Call(VariantWith<Channel::Connected>(_))).Times(1);
    channel2.subscribe(ch2_event_mock.AsStdFunction());

    // Emulate that server posted `RouteChannelMsg` on tag #0.
    //
    std::uint64_t tag = 0;
    std::uint64_t seq = 0;
    EXPECT_CALL(ch1_event_mock, Call(VariantWith<Channel::Input>(_))).Times(2);
    emulateRouteChannelMsg(client_pipe_mock, tag, Channel::Input{&mr_}, seq);
    emulateRouteChannelMsg(client_pipe_mock, tag, Channel::Input{&mr_}, seq);

    // Emulate that server posted `RouteChannelMsg` on tag #1.
    //
    tag = 1, seq = 0;
    EXPECT_CALL(ch2_event_mock, Call(VariantWith<Channel::Input>(_))).Times(1);
    emulateRouteChannelMsg(client_pipe_mock, tag, Channel::Input{&mr_}, seq);

    // Emulate that the pipe is disconnected - all channels should be notified.
    //
    EXPECT_CALL(ch1_event_mock, Call(VariantWith<Channel::Completed>(_))).Times(1);
    EXPECT_CALL(ch2_event_mock, Call(VariantWith<Channel::Completed>(_))).Times(1);
    client_pipe_mock.event_handler_(pipe::ClientPipe::Event::Disconnected{});
}

TEST_F(TestClientRouter, channel_unsolicited)
{
    using Msg     = ocvsmd::common::svc::node::ExecCmd::Request_0_1;
    using Channel = Channel<Msg, Msg>;

    StrictMock<pipe::ClientPipeMock> client_pipe_mock;
    EXPECT_CALL(client_pipe_mock, deinit()).Times(1);

    const auto client_router = ClientRouter::make(  //
        mr_,
        std::make_unique<pipe::ClientPipeMock::Wrapper>(client_pipe_mock));
    ASSERT_THAT(client_router, NotNull());

    EXPECT_CALL(client_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(client_router->start(), 0);

    StrictMock<MockFunction<void(const Channel::EventVar&)>> ch_event_mock;

    auto channel = client_router->makeChannel<Channel>();
    channel.subscribe(ch_event_mock.AsStdFunction());

    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Connected>(_))).Times(1);
    emulateRouteConnect(client_pipe_mock);

    // Emulate that server posted `RouteChannelMsg` on unknown tag.
    //
    constexpr std::uint64_t tag = 0;
    std::uint64_t           seq = 0;
    emulateRouteChannelMsg(client_pipe_mock, tag + 1, Channel::Input{&mr_}, seq);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, bugprone-unchecked-optional-access)

}  // namespace
