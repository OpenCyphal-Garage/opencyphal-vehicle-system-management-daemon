//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "engine/application.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <unistd.h>

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
    for (int fd = first_fd_to_close; fd <= rlimit_files.rlim_cur; ++fd)
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
    // Not sure what to sanitize exactly.
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

void step_12_create_pid_file(const int pipe_write_fd)
{
    const int fd = ::open("/var/run/ocvsmd.pid", O_RDWR | O_CREAT, 0644);  // NOLINT *-vararg
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
    // Not sure what to drop exactly.
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
int daemonize()
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
        step_12_create_pid_file(pipe_write_fd);
        step_13_drop_privileges();

        // `step_14_notify_init_complete(pipe_write_fd);` will be called by the main
        // when the application has been successfully initialized.
        return pipe_write_fd;
    }

    // Original parent process.
    assert(pipe_fds[0] != -1);
    assert(pipe_fds[1] == -1);
    auto& pipe_read_fd = pipe_fds[0];

    step_15_exit_org_process(pipe_read_fd);
    return -1;  // Unreachable actually b/c of `::exit` call.
}

}  // namespace

int main(const int argc, const char** const argv)
{
    using ocvsmd::daemon::engine::Application;

    bool should_daemonize = true;
    for (int i = 1; i < argc; ++i)
    {
        if (::strcmp(argv[i], "--dev") == 0)  // NOLINT
        {
            should_daemonize = false;
        }
    }

    // If daemonizing is disabled (in dev mode) then use the standard error output for reporting,
    // otherwise use the pipe to communicate with the parent process.
    int pipe_write_fd = 2;
    if (should_daemonize)
    {
        pipe_write_fd = daemonize();
        // We are in a child process now!
    }

    Application application;
    if (const auto failure_str = application.init())
    {
        write_string(pipe_write_fd, "Failed to init application: ");
        write_string(pipe_write_fd, failure_str.value().c_str());
        ::exit(EXIT_FAILURE);
    }
    if (should_daemonize)
    {
        step_14_notify_init_complete(pipe_write_fd);
    }

    ::openlog("ocvsmd", LOG_PID, LOG_DAEMON);
    ::syslog(LOG_NOTICE, "ocvsmd daemon started.");  // NOLINT *-vararg

    application.runWhile([] { return s_running == 1; });

    ::syslog(LOG_NOTICE, "ocvsmd daemon terminated.");  // NOLINT *-vararg
    ::closelog();

    return EXIT_SUCCESS;
}
