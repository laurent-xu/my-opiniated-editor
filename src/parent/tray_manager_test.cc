#include "src/parent/tray_manager.h"

#include <poll.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/workspace_parent.h"

namespace {

using namespace std::chrono_literals;

std::filesystem::path required_env_path(char const* name) {
  char const* value = std::getenv(name);
  if (value == nullptr) {
    throw std::runtime_error(std::string("missing environment variable: ") + name);
  }
  return {value};
}

std::unique_ptr<moe::parent::TrayManager> start_manager() {
  return moe::parent::TrayManager::start(moe::parent::TrayConfig{
      .command = moe::parent::interactive_shell_command(moe::parent::configured_login_shell()),
      .working_directory = required_env_path("TEST_TMPDIR"),
      .initial_size = {.rows = 24, .cols = 80},
  });
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

std::string read_until(moe::parent::TrayManager& manager, std::string const& needle) {
  std::string output;
  auto const deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd descriptor{
        .fd = manager.active_content_file_descriptor().value(), .events = POLLIN, .revents = 0};
    int const result = poll(&descriptor, 1, 100);
    if (result <= 0 || (descriptor.revents & POLLIN) == 0) {
      continue;
    }

    std::optional<std::string> chunk = manager.read_active_output();
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

TEST(TrayNumberTest, OnlyTrayOneExistsForNow) {
  std::optional<moe::parent::TrayNumber> const tray_one = moe::parent::TrayNumber::from_int(1);
  ASSERT_TRUE(tray_one.has_value());
  EXPECT_EQ(moe::parent::TrayNumber::one().value(), 1);
  EXPECT_FALSE(moe::parent::TrayNumber::from_int(0).has_value());
  EXPECT_FALSE(moe::parent::TrayNumber::from_int(2).has_value());
}

TEST(TrayManagerTest, StartsWithAnonymousTrayOne) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();

  moe::parent::TraySnapshot const active = manager->active_snapshot();
  EXPECT_EQ(active.id.key(), "anonymous:1");
  EXPECT_EQ(active.id.label(), "tray 1");
  EXPECT_EQ(active.id.anonymous_number().value(), 1);
  EXPECT_EQ(active.label, "tray 1");
  EXPECT_EQ(active.working_directory, required_env_path("TEST_TMPDIR"));
  EXPECT_GT(active.child_pid.value(), 0);
  EXPECT_TRUE(manager->active_content_file_descriptor().is_valid());
}

TEST(TrayManagerTest, RoutesInputAndOutputThroughActiveTray) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();

  manager->write_input(shell_marker_command("__moe_tray_one_output__"));
  std::string const output = read_until(*manager, "__moe_tray_one_output__");

  EXPECT_NE(output.find("__moe_tray_one_output__"), std::string::npos);
}

TEST(TrayManagerTest, ResizeAppliesToActiveTray) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();

  manager->resize_active(moe::parent::TerminalSize{.rows = 33, .cols = 111});
  manager->write_input("stty size\n");
  manager->write_input(shell_marker_command("__moe_resize_done__"));

  std::string const output = read_until(*manager, "__moe_resize_done__");
  EXPECT_NE(output.find("33 111"), std::string::npos);
}

}  // namespace
