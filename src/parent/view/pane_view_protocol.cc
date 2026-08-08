#include "src/parent/view/pane_view_protocol.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace moe::parent {
namespace {

constexpr std::uint8_t OUTPUT_MESSAGE = 0;
constexpr std::uint8_t RESIZE_MESSAGE = 1;
constexpr std::size_t FRAME_HEADER_SIZE = 4;
constexpr std::size_t IDENTITY_SIZE_WITHOUT_TRAY = 2 + 8;
constexpr std::size_t RESIZE_PAYLOAD_SIZE = 8;
constexpr std::uint32_t MAX_FRAME_SIZE = 1024U * 1024U;

void append_u16(std::string& output, std::uint16_t const value) {
  output.push_back(static_cast<char>((value >> 8U) & 0xffU));
  output.push_back(static_cast<char>(value & 0xffU));
}

void append_u32(std::string& output, std::uint32_t const value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    output.push_back(static_cast<char>((value >> static_cast<unsigned int>(shift)) & 0xffU));
  }
}

void append_u64(std::string& output, std::uint64_t const value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    output.push_back(static_cast<char>((value >> static_cast<unsigned int>(shift)) & 0xffU));
  }
}

std::uint16_t read_u16(std::string_view const input, std::size_t const offset) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(static_cast<unsigned char>(input.at(offset))) << 8U) |
      static_cast<unsigned char>(input.at(offset + 1U)));
}

std::uint32_t read_u32(std::string_view const input, std::size_t const offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4U; ++index) {
    value = (value << 8U) | static_cast<unsigned char>(input.at(offset + index));
  }
  return value;
}

std::uint64_t read_u64(std::string_view const input, std::size_t const offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8U; ++index) {
    value = (value << 8U) | static_cast<unsigned char>(input.at(offset + index));
  }
  return value;
}

void append_identity(std::string& output, std::string_view const tray_key, PaneId const pane_id) {
  if (tray_key.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw std::invalid_argument("pane view tray key is too long");
  }
  append_u16(output, static_cast<std::uint16_t>(tray_key.size()));
  output.append(tray_key);
  append_u64(output, pane_id.value());
}

struct DecodedIdentity {
  std::string tray_key;
  PaneId pane_id;
  std::size_t next_offset;
};

std::optional<DecodedIdentity> decode_identity(std::string_view const payload) {
  if (payload.size() < IDENTITY_SIZE_WITHOUT_TRAY) {
    return std::nullopt;
  }
  std::uint16_t const tray_length = read_u16(payload, 0);
  std::size_t const pane_offset = 2U + tray_length;
  if (payload.size() < pane_offset + 8U) {
    return std::nullopt;
  }
  std::optional<PaneId> const pane_id = PaneId::from_value(read_u64(payload, pane_offset));
  if (!pane_id.has_value()) {
    return std::nullopt;
  }
  return DecodedIdentity{
      .tray_key = std::string(payload.substr(2U, tray_length)),
      .pane_id = *pane_id,
      .next_offset = pane_offset + 8U,
  };
}

std::string frame_body(PaneViewMessage const& message) {
  std::string body;
  std::visit(
      [&body](auto const& value) {
        using Message = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Message, PaneViewOutput>) {
          body.push_back(static_cast<char>(OUTPUT_MESSAGE));
          body.append(encode_pane_output_payload(value));
        } else {
          if (value.size.rows <= 0 || value.size.cols <= 0) {
            throw std::invalid_argument("pane view resize must be positive");
          }
          body.push_back(static_cast<char>(RESIZE_MESSAGE));
          append_identity(body, value.tray_key, value.pane_id);
          append_u32(body, static_cast<std::uint32_t>(value.size.rows));
          append_u32(body, static_cast<std::uint32_t>(value.size.cols));
        }
      },
      message);
  return body;
}

}  // namespace

std::string encode_pane_view_frame(PaneViewMessage const& message) {
  std::string const body = frame_body(message);
  if (body.size() > MAX_FRAME_SIZE) {
    throw std::invalid_argument("pane view message is too large");
  }
  std::string frame;
  frame.reserve(FRAME_HEADER_SIZE + body.size());
  append_u32(frame, static_cast<std::uint32_t>(body.size()));
  frame.append(body);
  return frame;
}

std::optional<PaneViewMessage> decode_pane_view_frame(std::string& buffer) {
  if (buffer.size() < FRAME_HEADER_SIZE) {
    return std::nullopt;
  }
  std::uint32_t const body_size = read_u32(buffer, 0);
  if (body_size == 0 || body_size > MAX_FRAME_SIZE) {
    throw std::runtime_error("invalid pane view frame size");
  }
  if (buffer.size() < FRAME_HEADER_SIZE + body_size) {
    return std::nullopt;
  }

  std::string const body = buffer.substr(FRAME_HEADER_SIZE, body_size);
  buffer.erase(0, FRAME_HEADER_SIZE + body_size);
  std::string_view const payload(body.data() + 1U, body.size() - 1U);
  if (static_cast<unsigned char>(body.front()) == OUTPUT_MESSAGE) {
    std::optional<DecodedIdentity> const identity = decode_identity(payload);
    if (!identity.has_value()) {
      throw std::runtime_error("invalid pane output identity");
    }
    return PaneViewOutput{
        .tray_key = identity->tray_key,
        .pane_id = identity->pane_id,
        .bytes = std::string(payload.substr(identity->next_offset)),
    };
  }
  if (static_cast<unsigned char>(body.front()) == RESIZE_MESSAGE) {
    std::optional<PaneViewResize> const resize = decode_pane_resize_payload(payload);
    if (!resize.has_value()) {
      throw std::runtime_error("invalid pane resize payload");
    }
    return *resize;
  }
  throw std::runtime_error("invalid pane view message type");
}

std::string encode_pane_output_payload(PaneViewOutput const& output) {
  std::string payload;
  append_identity(payload, output.tray_key, output.pane_id);
  payload.append(output.bytes);
  return payload;
}

std::optional<PaneViewResize> decode_pane_resize_payload(std::string_view const payload) {
  std::optional<DecodedIdentity> const identity = decode_identity(payload);
  if (!identity.has_value() || payload.size() != identity->next_offset + RESIZE_PAYLOAD_SIZE) {
    return std::nullopt;
  }
  std::uint32_t const rows = read_u32(payload, identity->next_offset);
  std::uint32_t const cols = read_u32(payload, identity->next_offset + 4U);
  if (rows == 0 || cols == 0 ||
      rows > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      cols > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  return PaneViewResize{
      .tray_key = identity->tray_key,
      .pane_id = identity->pane_id,
      .size = {.rows = static_cast<int>(rows), .cols = static_cast<int>(cols)},
  };
}

}  // namespace moe::parent
