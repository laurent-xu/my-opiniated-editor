#include "src/bridge/parent_pty_session.h"

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

std::unique_ptr<moe::bridge::ParentPtySession> start_parent(moe::bridge::PtySize const size = {
                                                                .rows = 24, .cols = 80}) {
  std::vector<std::string> command{runfile_path("src/parent/workspace_parent").string()};
  return moe::bridge::ParentPtySession::start(command, required_env_path("TEST_TMPDIR"), size);
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

TEST(ParentPtySessionTest, StartsParentAndExchangesBytes) {
  std::unique_ptr<moe::bridge::ParentPtySession> session = start_parent();
  EXPECT_GT(session->child_pid().value(), 0);

  wait_for_shell_command(*session, "__moe_parent_pty_ready__");
}

TEST(ParentPtySessionTest, InvalidSizeReportsBoundsAndActualValues) {
  std::vector<std::string> const command{runfile_path("src/parent/workspace_parent").string()};

  try {
    static_cast<void>(moe::bridge::ParentPtySession::start(
        command, required_env_path("TEST_TMPDIR"), moe::bridge::PtySize{.rows = 0, .cols = 70000}));
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
