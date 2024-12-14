//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "calculator.capnp.h"

#include <capnp/list.h>
#include <capnp/message.h>
#include <capnp/rpc-twoparty.h>
#include <kj/array.h>
#include <kj/async-io.h>
#include <kj/async.h>
#include <kj/common.h>
#include <kj/debug.h>
#include <kj/memory.h>

#include <array>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <signal.h>  // NOLINT *-deprecated-headers for `pid_t` type
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <unistd.h>

namespace
{

kj::Promise<double> readValue(Calculator::Value::Client value)
{
    return value.readRequest()
        .send()  //
        .then([](const capnp::Response<Calculator::Value::ReadResults>& result) { return result.getValue(); });
}

// NOLINTNEXTLINE(misc-no-recursion)
kj::Promise<double> evaluateImpl(const Calculator::Expression::Reader& expression,
                                 const capnp::List<double>::Reader&    params = capnp::List<double>::Reader())
{
    switch (expression.which())
    {
    case Calculator::Expression::LITERAL: {
        return expression.getLiteral();
    }

    case Calculator::Expression::PREVIOUS_RESULT: {
        return readValue(expression.getPreviousResult());
    }

    case Calculator::Expression::PARAMETER: {
        KJ_REQUIRE(expression.getParameter() < params.size(), "Parameter index out-of-range.");  // NOLINT
        return params[expression.getParameter()];
    }

    case Calculator::Expression::CALL: {
        auto call = expression.getCall();
        auto func = call.getFunction();

        // Evaluate each parameter.
        kj::Array<kj::Promise<double>> paramPromises = KJ_MAP(param, call.getParams())
        {
            return evaluateImpl(param, params);
        };

        // Join the array of promises into a promise for an array.
        kj::Promise<kj::Array<double>> joinedParams = kj::joinPromises(kj::mv(paramPromises));

        // When the parameters are complete, call the function.
        return joinedParams.then([KJ_CPCAP(func)](const kj::Array<double>& paramValues) mutable {
            auto request = func.callRequest();
            request.setParams(paramValues);
            return request.send().then(
                [](const capnp::Response<Calculator::Function::CallResults>& result) { return result.getValue(); });
        });
    }

    default: {
        // Throw an exception.
        KJ_FAIL_REQUIRE("Unknown expression type.");  // NOLINT
    }
    }
}

// No lint b/c we won't delete instances via (code generated!) base class (which has no virtual dtor).
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
class ValueImpl final : public Calculator::Value::Server
{
public:
    explicit ValueImpl(const double value)
        : value(value)
    {
    }

    kj::Promise<void> read(ReadContext context) override
    {
        context.getResults().setValue(value);
        return kj::READY_NOW;
    }

private:
    double value;
};

// No lint b/c we won't delete instances via (code generated!) base class (which has no virtual dtor).
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
class FunctionImpl final : public Calculator::Function::Server
{
public:
    FunctionImpl(const std::uint32_t paramCount, Calculator::Expression::Reader body)
        : paramCount(paramCount)
    {
        this->body.setRoot(body);
    }

    kj::Promise<void> call(CallContext context) override
    {
        auto params = context.getParams().getParams();
        KJ_REQUIRE(params.size() == paramCount, "Wrong number of parameters.");  // NOLINT

        return evaluateImpl(body.getRoot<Calculator::Expression>(), params)
            .then([KJ_CPCAP(context)](const double value) mutable { context.getResults().setValue(value); });
    }

private:
    std::uint32_t paramCount;

    capnp::MallocMessageBuilder body;
};

// No lint b/c we won't delete instances via (code generated!) base class (which has no virtual dtor).
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
class OperatorImpl final : public Calculator::Function::Server
{
public:
    explicit OperatorImpl(const Calculator::Operator op)
        : op(op)
    {
    }

    kj::Promise<void> call(CallContext context) override
    {
        auto params = context.getParams().getParams();
        KJ_REQUIRE(params.size() == 2, "Wrong number of parameters.");  // NOLINT

        double result = 0.0;
        switch (op)
        {
        case Calculator::Operator::ADD:  // NOLINT
            result = params[0] + params[1];
            break;
        case Calculator::Operator::SUBTRACT:
            result = params[0] - params[1];
            break;
        case Calculator::Operator::MULTIPLY:
            result = params[0] * params[1];
            break;
        case Calculator::Operator::DIVIDE:
            result = params[0] / params[1];
            break;
        default:
            KJ_FAIL_REQUIRE("Unknown operator.");  // NOLINT
            break;
        }

        context.getResults().setValue(result);
        return kj::READY_NOW;
    }

private:
    Calculator::Operator op;
};

// No lint b/c we won't delete instances via (code generated!) base class (which has no virtual dtor).
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
class CalculatorImpl final : public Calculator::Server
{
public:
    kj::Promise<void> evaluate(EvaluateContext context) override
    {
        return evaluateImpl(context.getParams().getExpression()).then([KJ_CPCAP(context)](double value) mutable {
            context.getResults().setValue(kj::heap<ValueImpl>(value));
        });
    }

    kj::Promise<void> defFunction(DefFunctionContext context) override
    {
        auto params = context.getParams();
        context.getResults().setFunc(kj::heap<FunctionImpl>(params.getParamCount(), params.getBody()));
        return kj::READY_NOW;
    }

    kj::Promise<void> getOperator(GetOperatorContext context) override
    {
        context.getResults().setFunc(kj::heap<OperatorImpl>(context.getParams().getOp()));
        return kj::READY_NOW;
    }
};

}  // namespace

namespace
{

const auto* const s_init_complete = "init_complete";

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile int s_running = 1;

extern "C" void handle_signal(const int sig)
{
    switch (sig)
    {
    case SIGTERM:
    case SIGINT:
        s_running = 0;
        break;
    default:
        break;
    }
}

bool write_string(const int fd, const char* const str)
{
    const auto str_len = strlen(str);
    return str_len == ::write(fd, str, str_len);
}

void exit_with_failure(const int fd, const char* const msg)
{
    const char* const err_txt = strerror(errno);
    write_string(fd, msg);
    write_string(fd, err_txt);
    ::exit(EXIT_FAILURE);
}

void step_01_close_all_file_descriptors(std::array<int, 2>& pipe_fds)
{
    rlimit rlimit_files{};
    if (getrlimit(RLIMIT_NOFILE, &rlimit_files) != 0)
    {
        const char* const err_txt = strerror(errno);
        std::cerr << "Failed to getrlimit(RLIMIT_NOFILE): " << err_txt << "\n";
        ::exit(EXIT_FAILURE);
    }
    constexpr int first_fd_to_close = 3;  // 0, 1 & 2 for standard input, output, and error.
    for (int fd = first_fd_to_close; fd <= rlimit_files.rlim_max; ++fd)
    {
        (void) ::close(fd);
    }

    // Create a pipe to communicate with the original process.
    //
    if (::pipe(pipe_fds.data()) == -1)
    {
        const char* const err_txt = ::strerror(errno);
        std::cerr << "Failed to create pipe: " << err_txt << "\n";
        ::exit(EXIT_FAILURE);
    }
}

void step_02_03_setup_signal_handlers()
{
    // Catch termination signals
    (void) ::signal(SIGTERM, handle_signal);
    (void) ::signal(SIGINT, handle_signal);
}

void step_04_sanitize_environment()
{
    // TODO: Implement this step.
}

bool step_05_fork_to_background(std::array<int, 2>& pipe_fds)
{
    // Fork off the parent process
    const pid_t parent_pid = fork();
    if (parent_pid < 0)
    {
        const char* const err_txt = ::strerror(errno);
        std::cerr << "Failed to fork: " << err_txt << "\n";
        ::exit(EXIT_FAILURE);
    }

    if (parent_pid == 0)
    {
        // Close read end on the child side.
        ::close(pipe_fds[0]);
        pipe_fds[0] = -1;
    }
    else
    {
        // Close write end on the parent side.
        ::close(pipe_fds[1]);
        pipe_fds[1] = -1;
    }

    return parent_pid == 0;
}

void step_06_create_new_session(const int pipe_write_fd)
{
    if (::setsid() < 0)
    {
        exit_with_failure(pipe_write_fd, "Failed to setsid: ");
    }
}

void step_07_08_fork_and_exit_again(int& pipe_write_fd)
{
    assert(pipe_write_fd != -1);

    // Fork off the parent process
    const pid_t pid = fork();
    if (pid < 0)
    {
        exit_with_failure(pipe_write_fd, "Failed to fork: ");
    }
    if (pid > 0)
    {
        ::close(pipe_write_fd);
        pipe_write_fd = -1;
        ::exit(EXIT_SUCCESS);
    }
}

void step_09_redirect_stdio_to_devnull(const int pipe_write_fd)
{
    const int fd = ::open("/dev/null", O_RDWR);  // NOLINT *-vararg
    if (fd == -1)
    {
        exit_with_failure(pipe_write_fd, "Failed to open(/dev/null): ");
    }

    ::dup2(fd, STDIN_FILENO);
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);

    if (fd > 2)
    {
        ::close(fd);
    }
}

void step_10_reset_umask()
{
    ::umask(0);
}

void step_11_change_curr_dir(const int pipe_write_fd)
{
    if (::chdir("/") != 0)
    {
        exit_with_failure(pipe_write_fd, "Failed to chdir(/): ");
    }
}

void step_12_create_pid_file(const int pipe_write_fd, const char* const pid_file_name)
{
    const int fd = ::open(pid_file_name, O_RDWR | O_CREAT, 0644);  // NOLINT *-vararg
    if (fd == -1)
    {
        exit_with_failure(pipe_write_fd, "Failed to create on PID file: ");
    }

    if (::lockf(fd, F_TLOCK, 0) == -1)
    {
        exit_with_failure(pipe_write_fd, "Failed to lock PID file: ");
    }

    if (::ftruncate(fd, 0) != 0)
    {
        exit_with_failure(pipe_write_fd, "Failed to ftruncate PID file: ");
    }

    constexpr std::size_t             max_pid_str_len = 32;
    std::array<char, max_pid_str_len> buf{};
    const auto len = ::snprintf(buf.data(), buf.size(), "%ld\n", static_cast<long>(::getpid()));  // NOLINT *-vararg
    if (::write(fd, buf.data(), len) != len)
    {
        exit_with_failure(pipe_write_fd, "Failed to write to PID file: ");
    }

    // Keep the PID file open until the process exits.
}

void step_13_drop_privileges()
{
    // n the daemon process, drop privileges, if possible and applicable.
    // TODO: Implement this step.
}

void step_14_notify_init_complete(int& pipe_write_fd)
{
    assert(pipe_write_fd != -1);

    // From the daemon process, notify the original process started that initialization is complete. This can be
    // implemented via an unnamed pipe or similar communication channel created before the first fork() and
    // hence available in both the original and the daemon process.

    // Closing the writing end of the pipe will signal the original process that the daemon is ready.
    write_string(pipe_write_fd, s_init_complete);
    ::close(pipe_write_fd);
    pipe_write_fd = -1;
}

void step_15_exit_org_process(int& pipe_read_fd)
{
    // Call exit() in the original process. The process that invoked the daemon must be able to rely on that this exit()
    // happens after initialization is complete and all external communication channels are established and accessible.

    constexpr std::size_t      buf_size = 256;
    std::array<char, buf_size> msg_from_child{};
    const auto                 res = ::read(pipe_read_fd, msg_from_child.data(), msg_from_child.size() - 1);
    if (res == -1)
    {
        const char* const err_txt = ::strerror(errno);
        std::cerr << "Failed to read pipe: " << err_txt << "\n";
        ::exit(EXIT_FAILURE);
    }

    if (::strcmp(msg_from_child.data(), s_init_complete) != 0)
    {
        std::cerr << "Child init failed: " << msg_from_child.data() << "\n";
        ::exit(EXIT_FAILURE);
    }

    ::close(pipe_read_fd);
    pipe_read_fd = -1;
    ::exit(EXIT_SUCCESS);
}

/// Implements the daemonization procedure as described in the `man 7 daemon` manual page.
///
void daemonize()
{
    std::array<int, 2> pipe_fds{-1, -1};

    step_01_close_all_file_descriptors(pipe_fds);
    step_02_03_setup_signal_handlers();
    step_04_sanitize_environment();
    if (step_05_fork_to_background(pipe_fds))
    {
        // Child process.
        assert(pipe_fds[0] == -1);
        assert(pipe_fds[1] != -1);
        auto& pipe_write_fd = pipe_fds[1];

        step_06_create_new_session(pipe_write_fd);
        step_07_08_fork_and_exit_again(pipe_write_fd);
        step_09_redirect_stdio_to_devnull(pipe_write_fd);
        step_10_reset_umask();
        step_11_change_curr_dir(pipe_write_fd);
        step_12_create_pid_file(pipe_write_fd, "/var/run/ocvsmd.pid");
        step_13_drop_privileges();
        step_14_notify_init_complete(pipe_write_fd);
    }
    else
    {
        // Original parent process.
        assert(pipe_fds[0] != -1);
        assert(pipe_fds[1] == -1);
        auto& pipe_read_fd = pipe_fds[0];

        step_15_exit_org_process(pipe_read_fd);
    }
}

}  // namespace

int main(const int argc, const char** const argv)
{
    bool is_dev = false;
    for (int i = 1; i < argc; ++i)
    {
        if (::strcmp(argv[i], "--dev") == 0)  // NOLINT
        {
            is_dev = true;
        }
    }

    if (!is_dev)
    {
        daemonize();
    }

    ::openlog("ocvsmd", LOG_PID, is_dev ? LOG_USER : LOG_DAEMON);
    ::syslog(LOG_NOTICE, "ocvsmd daemon started.");  // NOLINT *-vararg

    // First, we need to set up the KJ async event loop. This should happen one
    // per thread that needs to perform RPC.
    auto io = kj::setupAsyncIo();

    // Using KJ APIs, let's parse our network address and listen on it.
    kj::Network&                    network  = io.provider->getNetwork();
    kj::Own<kj::NetworkAddress>     addr     = network.parseAddress("127.0.0.1", 5923).wait(io.waitScope);  // NOLINT
    kj::Own<kj::ConnectionReceiver> listener = addr->listen();

    // Write the port number to stdout, in case it was chosen automatically.
    const auto port = listener->getPort();
    if (port == 0)
    {
        // The address format "unix:/path/to/socket" opens a unix domain socket,
        // in which case the port will be zero.
        std::cout << "Listening on Unix socket...\n" << std::endl;  // NOLINT performance-avoid-endl
    }
    else
    {
        std::cout << "Listening on port " << port << "..." << std::endl;  // NOLINT performance-avoid-endl
    }

    // Start the RPC server.
    capnp::TwoPartyServer server(kj::heap<CalculatorImpl>());

    server.listen(*listener).wait(io.waitScope);

    while (s_running == 1)
    {
        // TODO: Insert daemon code here.
        ::sleep(1);
    }

    ::syslog(LOG_NOTICE, "ocvsmd daemon terminated.");  // NOLINT *-vararg
    ::closelog();

    return EXIT_SUCCESS;
}
