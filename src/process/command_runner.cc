#include "src/process/command_runner.h"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/base/owned_file_descriptor.h"
#include "src/base/process_id.h"

namespace moe::process {
namespace {

std::runtime_error errno_error(std::string const& action) {
  int const error = errno;
  return std::runtime_error(action + ": " + std::generic_category().message(error));
}

std::vector<char*> command_argv(std::vector<std::string> const& command) {
  std::vector<char*> arguments;
  arguments.reserve(command.size() + 1U);
  for (std::string const& part : command) {
    arguments.push_back(const_cast<char*>(part.c_str()));
  }
  arguments.push_back(nullptr);
  return arguments;
}

ProcessExitStatus exit_status_from_wait_status(int const wait_status) {
  if (WIFEXITED(wait_status)) {
    return ProcessExitStatus::exited(WEXITSTATUS(wait_status));
  }
  if (WIFSIGNALED(wait_status)) {
    return ProcessExitStatus::signaled(WTERMSIG(wait_status));
  }
  return ProcessExitStatus::exited(1);
}

}  // namespace

CommandResult run_command(std::vector<std::string> const& command,
                          StandardOutputMode const standard_output_mode) {
  if (command.empty()) {
    throw std::invalid_argument("command must not be empty");
  }

  bool const capture_standard_output = standard_output_mode == StandardOutputMode::CAPTURE;
  std::array<int, 2> raw_pipe{-1, -1};
  if (capture_standard_output && ::pipe(raw_pipe.data()) != 0) {
    throw errno_error("create command output pipe");
  }
  base::OwnedFileDescriptor read_end(capture_standard_output ? base::FileDescriptor(raw_pipe[0])
                                                             : base::FileDescriptor{});
  base::OwnedFileDescriptor write_end(capture_standard_output ? base::FileDescriptor(raw_pipe[1])
                                                              : base::FileDescriptor{});

  base::ProcessId const child_pid(::fork());
  if (child_pid.is_error()) {
    throw errno_error("fork command");
  }
  if (child_pid.is_child_process()) {
    if (capture_standard_output) {
      read_end.reset();
      if (::dup2(write_end.get().value(), STDOUT_FILENO) < 0) {
        _exit(126);
      }
      write_end.reset();
    }
    std::vector<char*> arguments = command_argv(command);
    ::execvp(arguments[0], arguments.data());
    _exit(127);
  }

  write_end.reset();
  std::string output;
  if (capture_standard_output) {
    std::array<char, 4096> buffer{};
    while (true) {
      ssize_t const count = ::read(read_end.get().value(), buffer.data(), buffer.size());
      if (count > 0) {
        output.append(buffer.data(), static_cast<std::size_t>(count));
        continue;
      }
      if (count == 0) {
        break;
      }
      if (errno != EINTR) {
        throw errno_error("read command output");
      }
    }
  }

  int wait_status = 0;
  base::ProcessId waited;
  do {
    waited = base::ProcessId(::waitpid(child_pid.value(), &wait_status, 0));
  } while (waited.is_error() && errno == EINTR);
  if (waited.value() != child_pid.value()) {
    throw errno_error("wait for command");
  }
  return {
      .exit_status = exit_status_from_wait_status(wait_status),
      .standard_output = std::move(output),
  };
}

}  // namespace moe::process
