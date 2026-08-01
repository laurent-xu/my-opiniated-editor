#include "src/bridge/protocol/browser_application_message_parser_test_support.h"

#include <stdexcept>

#include "gtest/gtest.h"
#include "src/bridge/protocol/browser_application_message_parser.h"

namespace moe::bridge::protocol::test {

void expect_parse_error(ParseErrorExpectation const& expectation) {
  try {
    static_cast<void>(parse_browser_application_message(expectation.message));
    FAIL() << "expected parse error for: " << expectation.message;
  } catch (std::runtime_error const& error) {
    EXPECT_EQ(error.what(), expectation.expected_error);
  }
}

}  // namespace moe::bridge::protocol::test
