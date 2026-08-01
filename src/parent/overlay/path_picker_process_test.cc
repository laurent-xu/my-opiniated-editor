#include "src/parent/overlay/path_picker_process.h"

#include <poll.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"

namespace {

std::filesystem::path required_environment_path(char const* const name) {
  char const* const value = std::getenv(name);
  if (value == nullptr) {
    throw std::runtime_error(std::string(name) + " is required");
  }
  return {value};
}

std::filesystem::path runfile_path(std::filesystem::path const& path) {
  return required_environment_path("TEST_SRCDIR") / required_environment_path("TEST_WORKSPACE") /
         path;
}

TEST(PathPickerProcessTest, RunsPickerThroughPtyAndReturnsRawSelection) {
  std::unique_ptr<moe::parent::PathPickerProcess> process = moe::parent::PathPickerProcess::start(
      runfile_path("test/fixtures/fake_fzf").string(), "Pick> ", {.rows = 12, .cols = 80});
  process->write_candidate_input(std::string_view("first\0second\0", 13));
  process->close_candidate_input();

  bool picker_rendered = false;
  for (int attempt = 0; attempt < 100 && !picker_rendered; ++attempt) {
    moe::base::FileDescriptor const descriptor =
        process->file_descriptor().value_or(moe::base::FileDescriptor{});
    ASSERT_TRUE(descriptor.is_valid());
    pollfd readable{.fd = descriptor.value(), .events = POLLIN, .revents = 0};
    if (::poll(&readable, 1, 50) == 1 && (readable.revents & POLLIN) != 0) {
      std::optional<std::string> const output = process->read_available();
      picker_rendered = output.has_value() && output->find("Pick> ") != std::string::npos;
    }
  }
  ASSERT_TRUE(picker_rendered);

  process->resize({.rows = 10, .cols = 41});
  process->write("\x1b[B\r");

  bool completed = false;
  for (int attempt = 0; attempt < 100 && !completed; ++attempt) {
    completed = process->refresh_process_state();
    if (!completed) {
      pollfd pause{.fd = -1, .events = 0, .revents = 0};
      static_cast<void>(::poll(&pause, 0, 10));
    }
  }

  ASSERT_TRUE(completed);
  EXPECT_TRUE(process->finished());
  EXPECT_TRUE(process->result_succeeded());
  EXPECT_EQ(process->read_result(), std::string("1\0", 2));
  EXPECT_FALSE(process->file_descriptor().has_value());
}

TEST(PathPickerProcessTest, RejectsInvalidConfigurationBeforeForking) {
  EXPECT_THROW(
      {
        [[maybe_unused]] auto process =
            moe::parent::PathPickerProcess::start("", "Pick> ", {.rows = 12, .cols = 80});
      },
      std::invalid_argument);
  EXPECT_THROW(
      {
        [[maybe_unused]] auto process =
            moe::parent::PathPickerProcess::start("fzf", "Pick> ", {.rows = 0, .cols = 80});
      },
      std::invalid_argument);
}

}  // namespace
