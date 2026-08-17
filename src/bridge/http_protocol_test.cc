#include "src/bridge/http_protocol.h"

#include <optional>
#include <string>

#include "gtest/gtest.h"

namespace {

TEST(HttpProtocolTest, FindsHeaderValueCaseInsensitively) {
  moe::bridge::HttpRequest const request{
      .method = "GET",
      .path = "/ws",
      .raw_headers =
          "GET /ws HTTP/1.1\r\nSec-Websocket-Key:\t tailscale-key\r\nConnection: Upgrade\r\n\r\n",
  };

  std::optional<std::string> const value = moe::bridge::header_value(request, "Sec-WebSocket-Key");

  EXPECT_EQ(value, std::optional<std::string>("tailscale-key"));
}

}  // namespace
