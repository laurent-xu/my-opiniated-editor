#include "src/parent/path_picker_overlay.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "gtest/gtest.h"
#include "src/base/terminal_size.h"

namespace {

static_assert(std::is_same_v<decltype(std::declval<moe::parent::PathPickerOverlay const&>()
                                          .available_region_above()),
                             moe::base::TerminalSize>);

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

TEST(PathPickerOverlayTest, ReportsAvailableRegionDimensionsAbovePicker) {
  std::unique_ptr<moe::parent::PathPickerOverlay> picker =
      moe::parent::PathPickerOverlay::start(runfile_path("test/fixtures/fake_fzf").string(),
                                            {"candidate"}, "Pick> ", {.rows = 24, .cols = 80});

  moe::base::TerminalSize const initial_region = picker->available_region_above();
  EXPECT_EQ(initial_region.rows, 12);
  EXPECT_EQ(initial_region.cols, 80);

  picker->resize({.rows = 10, .cols = 41});
  moe::base::TerminalSize const resized_region = picker->available_region_above();
  EXPECT_EQ(resized_region.rows, 4);
  EXPECT_EQ(resized_region.cols, 41);
}

TEST(PathPickerOverlayTest, ReportsEmptyRegionWhenPickerUsesFullHeight) {
  std::unique_ptr<moe::parent::PathPickerOverlay> picker =
      moe::parent::PathPickerOverlay::start(runfile_path("test/fixtures/fake_fzf").string(),
                                            {"candidate"}, "Pick> ", {.rows = 5, .cols = 37});

  moe::base::TerminalSize const region = picker->available_region_above();
  EXPECT_EQ(region.rows, 0);
  EXPECT_EQ(region.cols, 37);
}

}  // namespace
