#include "src/bridge/websocket_protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "src/bridge/socket_io.h"

namespace moe::bridge {
namespace {

constexpr std::string_view WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::string_view BASE64_ALPHABET =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::size_t MAX_FRAME_SIZE = static_cast<std::size_t>(1024) * 1024U;

std::uint32_t rotate_left(std::uint32_t const value, int const shift) {
  return (value << shift) | (value >> (32 - shift));
}

void append_big_endian_32(std::string& output, std::uint32_t const value) {
  output.push_back(static_cast<char>((value >> 24U) & 0xFFU));
  output.push_back(static_cast<char>((value >> 16U) & 0xFFU));
  output.push_back(static_cast<char>((value >> 8U) & 0xFFU));
  output.push_back(static_cast<char>(value & 0xFFU));
}

void append_big_endian_64(std::string& output, std::uint64_t const value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    output.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

std::array<std::uint8_t, 20> sha1_digest(std::string_view const message) {
  std::string padded(message);
  std::uint64_t const bit_length = static_cast<std::uint64_t>(padded.size()) * 8U;
  padded.push_back(static_cast<char>(0x80U));
  while ((padded.size() % 64U) != 56U) {
    padded.push_back('\0');
  }
  append_big_endian_64(padded, bit_length);

  std::uint32_t hash_0 = 0x67452301U;
  std::uint32_t hash_1 = 0xEFCDAB89U;
  std::uint32_t hash_2 = 0x98BADCFEU;
  std::uint32_t hash_3 = 0x10325476U;
  std::uint32_t hash_4 = 0xC3D2E1F0U;

  for (std::size_t chunk_start = 0; chunk_start < padded.size(); chunk_start += 64U) {
    std::array<std::uint32_t, 80> words{};
    for (std::size_t index = 0; index < 16U; ++index) {
      std::size_t const offset = chunk_start + (index * 4U);
      words[index] =
          (static_cast<std::uint32_t>(static_cast<unsigned char>(padded[offset])) << 24U) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(padded[offset + 1U])) << 16U) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(padded[offset + 2U])) << 8U) |
          static_cast<std::uint32_t>(static_cast<unsigned char>(padded[offset + 3U]));
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
      words[index] = rotate_left(
          words[index - 3U] ^ words[index - 8U] ^ words[index - 14U] ^ words[index - 16U], 1);
    }

    std::uint32_t a = hash_0;
    std::uint32_t b = hash_1;
    std::uint32_t c = hash_2;
    std::uint32_t d = hash_3;
    std::uint32_t e = hash_4;

    for (std::size_t index = 0; index < words.size(); ++index) {
      std::uint32_t function = 0;
      std::uint32_t constant = 0;
      if (index < 20U) {
        function = (b & c) | ((~b) & d);
        constant = 0x5A827999U;
      } else if (index < 40U) {
        function = b ^ c ^ d;
        constant = 0x6ED9EBA1U;
      } else if (index < 60U) {
        function = (b & c) | (b & d) | (c & d);
        constant = 0x8F1BBCDCU;
      } else {
        function = b ^ c ^ d;
        constant = 0xCA62C1D6U;
      }

      std::uint32_t const temp = rotate_left(a, 5) + function + e + constant + words[index];
      e = d;
      d = c;
      c = rotate_left(b, 30);
      b = a;
      a = temp;
    }

    hash_0 += a;
    hash_1 += b;
    hash_2 += c;
    hash_3 += d;
    hash_4 += e;
  }

  std::string digest_bytes;
  digest_bytes.reserve(20);
  append_big_endian_32(digest_bytes, hash_0);
  append_big_endian_32(digest_bytes, hash_1);
  append_big_endian_32(digest_bytes, hash_2);
  append_big_endian_32(digest_bytes, hash_3);
  append_big_endian_32(digest_bytes, hash_4);

  std::array<std::uint8_t, 20> digest{};
  for (std::size_t index = 0; index < digest.size(); ++index) {
    digest[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(digest_bytes[index]));
  }
  return digest;
}

std::string base64_encode(std::array<std::uint8_t, 20> const& bytes) {
  std::string output;
  output.reserve(28);
  for (std::size_t index = 0; index < bytes.size(); index += 3U) {
    std::uint32_t const first = bytes[index];
    std::uint32_t const second = (index + 1U < bytes.size()) ? bytes[index + 1U] : 0U;
    std::uint32_t const third = (index + 2U < bytes.size()) ? bytes[index + 2U] : 0U;
    std::uint32_t const triple = (first << 16U) | (second << 8U) | third;

    output.push_back(BASE64_ALPHABET[(triple >> 18U) & 0x3FU]);
    output.push_back(BASE64_ALPHABET[(triple >> 12U) & 0x3FU]);
    output.push_back(index + 1U < bytes.size() ? BASE64_ALPHABET[(triple >> 6U) & 0x3FU] : '=');
    output.push_back(index + 2U < bytes.size() ? BASE64_ALPHABET[triple & 0x3FU] : '=');
  }
  return output;
}

std::string websocket_accept_key(std::string const& client_key) {
  return base64_encode(sha1_digest(client_key + std::string(WEBSOCKET_GUID)));
}

}  // namespace

void send_websocket_frame(base::FileDescriptor const file_descriptor,
                          WebsocketFrame::Opcode const opcode, std::string_view const payload) {
  std::string frame;
  frame.push_back(static_cast<char>(0x80U | static_cast<std::uint8_t>(opcode)));
  if (payload.size() < 126U) {
    frame.push_back(static_cast<char>(payload.size()));
  } else if (payload.size() <= std::numeric_limits<std::uint16_t>::max()) {
    frame.push_back(static_cast<char>(126U));
    frame.push_back(static_cast<char>((payload.size() >> 8U) & 0xFFU));
    frame.push_back(static_cast<char>(payload.size() & 0xFFU));
  } else {
    frame.push_back(static_cast<char>(127U));
    append_big_endian_64(frame, payload.size());
  }
  frame.append(payload);
  send_all(file_descriptor, frame);
}

std::optional<WebsocketFrame> read_websocket_frame(base::FileDescriptor const file_descriptor) {
  std::optional<std::string> const header =
      read_exact_or_closed(file_descriptor, ByteCount{.value = 2});
  if (!header.has_value()) {
    return std::nullopt;
  }

  auto const first_byte = static_cast<std::uint8_t>((*header)[0]);
  auto const second_byte = static_cast<std::uint8_t>((*header)[1]);
  auto const opcode = static_cast<WebsocketFrame::Opcode>(first_byte & 0x0FU);
  bool const masked = (second_byte & 0x80U) != 0;
  std::uint64_t payload_length = second_byte & 0x7FU;

  if (payload_length == 126U) {
    std::optional<std::string> const extended =
        read_exact_or_closed(file_descriptor, ByteCount{.value = 2});
    if (!extended.has_value()) {
      return std::nullopt;
    }
    payload_length =
        (static_cast<std::uint64_t>(static_cast<unsigned char>((*extended)[0])) << 8U) |
        static_cast<std::uint64_t>(static_cast<unsigned char>((*extended)[1]));
  } else if (payload_length == 127U) {
    std::optional<std::string> const extended =
        read_exact_or_closed(file_descriptor, ByteCount{.value = 8});
    if (!extended.has_value()) {
      return std::nullopt;
    }
    payload_length = 0;
    for (char const byte : *extended) {
      payload_length =
          (payload_length << 8U) | static_cast<std::uint64_t>(static_cast<unsigned char>(byte));
    }
  }

  if (payload_length > MAX_FRAME_SIZE) {
    throw std::runtime_error("websocket frame too large");
  }

  std::array<std::uint8_t, 4> mask{};
  if (masked) {
    std::optional<std::string> const mask_bytes =
        read_exact_or_closed(file_descriptor, ByteCount{.value = mask.size()});
    if (!mask_bytes.has_value()) {
      return std::nullopt;
    }
    for (std::size_t index = 0; index < mask.size(); ++index) {
      mask[index] = static_cast<std::uint8_t>((*mask_bytes)[index]);
    }
  }

  std::optional<std::string> payload = read_exact_or_closed(
      file_descriptor, ByteCount{.value = static_cast<std::size_t>(payload_length)});
  if (!payload.has_value()) {
    return std::nullopt;
  }
  if (masked) {
    for (std::size_t index = 0; index < payload->size(); ++index) {
      (*payload)[index] = static_cast<char>(static_cast<std::uint8_t>((*payload)[index]) ^
                                            mask[index % mask.size()]);
    }
  }

  return WebsocketFrame{.opcode = opcode, .payload = *payload};
}

bool send_websocket_handshake(base::OwnedFileDescriptor const& client, HttpRequest const& request) {
  std::optional<std::string> const client_key = header_value(request, "Sec-WebSocket-Key");
  if (!client_key.has_value()) {
    send_http_response(client.get(), "400 Bad Request", "text/plain", "missing websocket key\n");
    return false;
  }

  std::string response =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: " +
      websocket_accept_key(*client_key) +
      "\r\n"
      "Sec-WebSocket-Protocol: workspace-pty\r\n"
      "\r\n";
  send_all(client.get(), response);
  return true;
}

}  // namespace moe::bridge
