#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "src/base/file_descriptor.h"
#include "src/base/owned_file_descriptor.h"
#include "src/bridge/http_protocol.h"

namespace moe::bridge {

struct WebsocketFrame {
  enum class Opcode : std::uint8_t {
    TEXT = 0x1U,
    BINARY = 0x2U,
    CLOSE = 0x8U,
  };

  Opcode opcode;
  std::string payload;
};

void send_websocket_frame(base::FileDescriptor file_descriptor, WebsocketFrame::Opcode opcode,
                          std::string_view payload);
[[nodiscard]] std::optional<WebsocketFrame> read_websocket_frame(
    base::FileDescriptor file_descriptor);
[[nodiscard]] bool send_websocket_handshake(base::OwnedFileDescriptor const& client,
                                            HttpRequest const& request);

}  // namespace moe::bridge
