#include "src/bridge/http_protocol.h"

#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <stdexcept>
#include <string_view>

#include "src/bridge/socket_io.h"

namespace moe::bridge {
namespace {

char ascii_lower(char const value) {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<char>(value + ('a' - 'A'));
  }
  return value;
}

bool header_names_equal(std::string_view const left, std::string_view const right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (ascii_lower(left[index]) != ascii_lower(right[index])) {
      return false;
    }
  }
  return true;
}

}  // namespace

HttpRequest read_http_request(base::FileDescriptor const file_descriptor) {
  std::string request;
  while (request.find("\r\n\r\n") == std::string::npos) {
    std::array<char, 4096> buffer{};
    ssize_t const received = ::recv(file_descriptor.value(), buffer.data(), buffer.size(), 0);
    if (received <= 0) {
      throw std::runtime_error("connection closed while reading HTTP request");
    }
    request.append(buffer.data(), static_cast<std::size_t>(received));
    if (request.size() > 16384U) {
      throw std::runtime_error("HTTP request headers too large");
    }
  }

  std::size_t const method_end = request.find(' ');
  std::size_t const path_end = request.find(' ', method_end + 1U);
  if (method_end == std::string::npos || path_end == std::string::npos) {
    throw std::runtime_error("malformed HTTP request line");
  }

  std::string const target = request.substr(method_end + 1U, path_end - method_end - 1U);
  std::string const path = target.substr(0, target.find('?'));

  return {.method = request.substr(0, method_end), .path = path, .raw_headers = request};
}

std::optional<std::string> header_value(HttpRequest const& request,
                                        std::string const& header_name) {
  std::size_t line_start = request.raw_headers.find("\r\n");
  if (line_start == std::string::npos) {
    return std::nullopt;
  }
  line_start += 2U;
  while (line_start < request.raw_headers.size()) {
    std::size_t const line_end = request.raw_headers.find("\r\n", line_start);
    if (line_end == std::string::npos || line_end == line_start) {
      return std::nullopt;
    }
    std::size_t const separator = request.raw_headers.find(':', line_start);
    if (separator != std::string::npos && separator < line_end &&
        header_names_equal(
            std::string_view(request.raw_headers).substr(line_start, separator - line_start),
            header_name)) {
      std::size_t value_start = separator + 1U;
      while (value_start < line_end && (request.raw_headers[value_start] == ' ' ||
                                        request.raw_headers[value_start] == '\t')) {
        ++value_start;
      }
      return request.raw_headers.substr(value_start, line_end - value_start);
    }
    line_start = line_end + 2U;
  }
  return std::nullopt;
}

void send_http_response(base::FileDescriptor const file_descriptor, std::string const& status,
                        std::string const& content_type, std::string const& body) {
  std::string response = "HTTP/1.1 " + status + "\r\nContent-Type: " + content_type +
                         "\r\nContent-Length: " + std::to_string(body.size()) +
                         "\r\nConnection: close\r\n\r\n" + body;
  send_all(file_descriptor, response);
}

}  // namespace moe::bridge
