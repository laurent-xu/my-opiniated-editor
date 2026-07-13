#include "src/bridge/http_protocol.h"

#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <stdexcept>

#include "src/bridge/socket_io.h"

namespace moe::bridge {
namespace {

struct QueryString {
  std::string_view value;
};

struct QueryParameterName {
  std::string_view value;
};

std::optional<std::string_view> query_parameter(QueryString const query,
                                                QueryParameterName const name) {
  std::size_t cursor = 0;
  while (cursor <= query.value.size()) {
    std::size_t const next = query.value.find('&', cursor);
    std::string_view const part = query.value.substr(
        cursor, next == std::string_view::npos ? std::string_view::npos : next - cursor);
    std::size_t const equals = part.find('=');
    if (equals != std::string_view::npos && part.substr(0, equals) == name.value) {
      return part.substr(equals + 1U);
    }
    if (next == std::string_view::npos) {
      break;
    }
    cursor = next + 1U;
  }
  return std::nullopt;
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
  std::size_t const query_start = target.find('?');
  std::string path = target;
  std::string query;
  if (query_start != std::string::npos) {
    path = target.substr(0, query_start);
    query = target.substr(query_start + 1U);
  }

  return {.method = request.substr(0, method_end),
          .path = path,
          .query = query,
          .raw_headers = request};
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

bool request_has_auth_token(HttpRequest const& request, std::string const& auth_token) {
  if (auth_token.empty()) {
    return true;
  }

  std::optional<std::string_view> const query_token =
      query_parameter(QueryString{.value = request.query}, QueryParameterName{.value = "token"});
  if (query_token.has_value() && *query_token == auth_token) {
    return true;
  }

  std::optional<std::string> const authorization = header_value(request, "Authorization");
  std::string const bearer_prefix = "Bearer ";
  return authorization.has_value() && authorization->starts_with(bearer_prefix) &&
         authorization->substr(bearer_prefix.size()) == auth_token;
}

bool path_requires_authentication(std::string_view const path) {
  return path == "/" || path == "/health" || path == "/ws";
}

void send_http_response(base::FileDescriptor const file_descriptor, std::string const& status,
                        std::string const& content_type, std::string const& body) {
  std::string response = "HTTP/1.1 " + status + "\r\nContent-Type: " + content_type +
                         "\r\nContent-Length: " + std::to_string(body.size()) +
                         "\r\nConnection: close\r\n\r\n" + body;
  send_all(file_descriptor, response);
}

void send_unauthorized(base::OwnedFileDescriptor const& client) {
  send_http_response(client.get(), "401 Unauthorized", "text/plain", "unauthorized\n");
}

}  // namespace moe::bridge
