#include "src/base/process_id.h"

#include <gtest/gtest.h>

#include <concepts>

namespace moe::base {
namespace {

static_assert(std::equality_comparable<ProcessId>);
static_assert(ProcessId(42) == ProcessId(42));
static_assert(ProcessId(42) != ProcessId(43));

TEST(ProcessIdTest, ComparesProcessIdentity) {
  EXPECT_EQ(ProcessId(42), ProcessId(42));
  EXPECT_NE(ProcessId(42), ProcessId(43));
}

}  // namespace
}  // namespace moe::base
