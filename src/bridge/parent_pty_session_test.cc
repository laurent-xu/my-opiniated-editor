#include "src/bridge/parent_pty_session.h"

#include <poll.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

using namespace std::chrono_literals;

std::filesystem::path required_env_path(char const* name) {
  char const* value = std::getenv(name);
  if (value == nullptr) {
    throw std::runtime_error(std::string("missing environment variable: ") + name);
  }
  return {value};
}

std::filesystem::path runfile_path(std::filesystem::path const& path) {
  return required_env_path("TEST_SRCDIR") / required_env_path("TEST_WORKSPACE") / path;
}

std::unique_ptr<moe::bridge::ParentPtySession> start_parent(moe::base::TerminalSize const size = {
                                                                .rows = 24, .cols = 80}) {
  std::filesystem::path const test_directory = required_env_path("TEST_TMPDIR");
  std::string const state_directory = (test_directory / "state").string();
  if (::setenv("MOE_STATE_DIRECTORY", state_directory.c_str(), 1) != 0) {
    throw std::runtime_error("failed to configure test state directory");
  }
  std::vector<std::string> command{runfile_path("src/parent/workspace_parent").string()};
  return moe::bridge::ParentPtySession::start(command, test_directory, size);
}

std::string shell_marker_command(std::string const& marker) {
  std::ostringstream command;
  command << "printf '";
  for (unsigned char const character : marker) {
    command << '\\' << std::oct << std::setw(3) << std::setfill('0') << static_cast<int>(character);
  }
  command << "\\012'\n";
  return command.str();
}

void wait_for_shell_command(moe::bridge::ParentPtySession const& session,
                            std::string const& marker) {
  session.write(shell_marker_command(marker));
  std::string const output = session.read_until(marker, 5s);
  EXPECT_NE(output.find(marker), std::string::npos);
}

std::string read_parent_status(moe::bridge::ParentPtySession const& session) {
  auto const deadline = std::chrono::steady_clock::now() + 5s;
  std::string output;

  while (std::chrono::steady_clock::now() < deadline) {
    auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    pollfd descriptor{
        .fd = session.status_file_descriptor().value(), .events = POLLIN, .revents = 0};
    int const result = poll(&descriptor, 1, static_cast<int>(remaining.count()));
    if (result <= 0) {
      continue;
    }

    std::array<char, 4096> buffer{};
    ssize_t const read_count =
        ::read(session.status_file_descriptor().value(), buffer.data(), buffer.size());
    if (read_count > 0) {
      output.append(buffer.data(), static_cast<std::size_t>(read_count));
      if (output.find('\n') != std::string::npos) {
        return output.substr(0, output.find('\n'));
      }
    }
  }
  throw std::runtime_error("timed out waiting for parent status; output was: " + output);
}

TEST(ParentPtySessionTest, StartsParentAndExchangesBytes) {
  std::unique_ptr<moe::bridge::ParentPtySession> session = start_parent();
  EXPECT_GT(session->child_pid().value(), 0);
  EXPECT_TRUE(session->file_descriptor().is_valid());
  EXPECT_TRUE(session->status_file_descriptor().is_valid());
  EXPECT_TRUE(session->view_file_descriptor().is_valid());

  wait_for_shell_command(*session, "__moe_parent_pty_ready__");
  EXPECT_EQ(
      read_parent_status(*session),
      R"({"type":"parent.status","commandMode":false,"trayKey":"anonymous:1","trayLabel":"tray 1","overlay":"none","paneMode":"none","paneSelectedNodes":0,"paneView":{"focusedPane":"1","maximized":false,"layout":{"id":"1","pane":"1"},"selection":null,"move":null}})");
}

TEST(ParentPtySessionTest, InvalidSizeReportsBoundsAndActualValues) {
  std::vector<std::string> const command{runfile_path("src/parent/workspace_parent").string()};

  try {
    static_cast<void>(
        moe::bridge::ParentPtySession::start(command, required_env_path("TEST_TMPDIR"),
                                             moe::base::TerminalSize{.rows = 0, .cols = 70000}));
    FAIL() << "expected invalid pty size";
  } catch (std::invalid_argument const& error) {
    std::string const message = error.what();

    EXPECT_NE(message.find("[1, 65535]"), std::string::npos);
    EXPECT_NE(message.find("actual rows=0 cols=70000"), std::string::npos);
  }
}

TEST(ParentPtySessionTest, ParentPersistsForSessionLifetime) {
  std::unique_ptr<moe::bridge::ParentPtySession> session = start_parent();
  auto const child_pid = session->child_pid();

  wait_for_shell_command(*session, "__moe_parent_pty_first__");
  wait_for_shell_command(*session, "__moe_parent_pty_second__");

  EXPECT_EQ(session->child_pid().value(), child_pid.value());
}

}  // namespace
