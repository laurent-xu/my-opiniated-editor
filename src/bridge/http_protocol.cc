#include "src/bridge/http_protocol.h"

#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <stdexcept>

#include "src/bridge/socket_io.h"

namespace moe::bridge {

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
  std::string const needle = "\r\n" + header_name + ":";
  std::size_t const position = request.raw_headers.find(needle);
  if (position == std::string::npos) {
    return std::nullopt;
  }
  std::size_t value_start = position + needle.size();
  while (value_start < request.raw_headers.size() && request.raw_headers[value_start] == ' ') {
    ++value_start;
  }
  std::size_t const value_end = request.raw_headers.find("\r\n", value_start);
  if (value_end == std::string::npos) {
    return std::nullopt;
  }
  return request.raw_headers.substr(value_start, value_end - value_start);
}

void send_http_response(base::FileDescriptor const file_descriptor, std::string const& status,
                        std::string const& content_type, std::string const& body) {
  std::string response = "HTTP/1.1 " + status + "\r\nContent-Type: " + content_type +
                         "\r\nContent-Length: " + std::to_string(body.size()) +
                         "\r\nConnection: close\r\n\r\n" + body;
  send_all(file_descriptor, response);
}

}  // namespace moe::bridge
