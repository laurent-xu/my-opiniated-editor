#include "src/base/network_port.h"

#include <gtest/gtest.h>

#include <type_traits>

namespace moe::base {
namespace {

static_assert(!std::is_convertible_v<int, NetworkPort>);

TEST(NetworkPortTest, AcceptsInclusivePortRangeBoundaries) {
  std::optional<NetworkPort> const minimum = NetworkPort::from_int(0);
  std::optional<NetworkPort> const maximum = NetworkPort::from_int(65535);

  if (minimum.has_value() && maximum.has_value()) {
    EXPECT_EQ(minimum->value(), 0);
    EXPECT_EQ(maximum->value(), 65535);
  } else {
    FAIL() << "inclusive network port boundaries must be valid";
  }
}

TEST(NetworkPortTest, RejectsValuesOutsidePortRange) {
  EXPECT_FALSE(NetworkPort::from_int(-1).has_value());
  EXPECT_FALSE(NetworkPort::from_int(65536).has_value());
}

}  // namespace
}  // namespace moe::base
