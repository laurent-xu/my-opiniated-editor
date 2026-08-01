#pragma once

#include <string_view>

namespace moe::bridge::protocol::test {

struct ParseErrorExpectation {
  std::string_view message;
  std::string_view expected_error;
};

void expect_parse_error(ParseErrorExpectation const& expectation);

}  // namespace moe::bridge::protocol::test
