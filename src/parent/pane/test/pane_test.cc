#include "src/parent/pane/pane.h"

#include <poll.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"
#include "src/parent/shell/shell_configuration.h"

namespace {

using namespace std::chrono_literals;

std::filesystem::path required_environment_path(char const* name) {
  char const* const value = std::getenv(name);
  if (value == nullptr) {
    throw std::runtime_error(std::string("missing environment variable: ") + name);
  }
  return {value};
}

std::string read_until(moe::parent::Pane& pane, std::string const& needle) {
  std::string output;
  auto const deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd descriptor{.fd = pane.file_descriptor().value(), .events = POLLIN, .revents = 0};
    int const result = poll(&descriptor, 1, 100);
    if (result <= 0 || (descriptor.revents & POLLIN) == 0) {
      continue;
    }

    std::optional<std::string> const chunk = pane.read_output();
    if (!chunk.has_value()) {
      continue;
    }
    output.append(*chunk);
    if (output.find(needle) != std::string::npos) {
      return output;
    }
  }

  throw std::runtime_error("timed out waiting for '" + needle + "'; output was: " + output);
}

TEST(PaneTest, OwnsContentProcessAndTerminalScreen) {
  std::unique_ptr<moe::parent::Pane> pane = moe::parent::Pane::start(moe::parent::PaneConfig{
      .command = moe::parent::interactive_shell_command(moe::parent::configured_login_shell()),
      .working_directory = required_environment_path("TEST_TMPDIR"),
      .initial_size = {.rows = 24, .cols = 80},
  });

  ASSERT_GT(pane->child_pid().value(), 0);
  ASSERT_TRUE(pane->file_descriptor().is_valid());

  pane->write_input("printf '__moe_pane_output__\\n'\n");
  std::string const output = read_until(*pane, "__moe_pane_output__");

  EXPECT_NE(output.find("__moe_pane_output__"), std::string::npos);
  EXPECT_NE(pane->redraw_output().find("__moe_pane_output__"), std::string::npos);
}

}  // namespace
