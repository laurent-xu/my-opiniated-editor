#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "src/base/file_descriptor.h"
#include "src/base/owned_file_descriptor.h"

namespace moe::bridge {

struct HttpRequest {
  std::string method;
  std::string path;
  std::string query;
  std::string raw_headers;
};

[[nodiscard]] HttpRequest read_http_request(base::FileDescriptor file_descriptor);
[[nodiscard]] std::optional<std::string> header_value(HttpRequest const& request,
                                                      std::string const& header_name);
[[nodiscard]] bool request_has_auth_token(HttpRequest const& request,
                                          std::string const& auth_token);
[[nodiscard]] bool path_requires_authentication(std::string_view path);
void send_http_response(base::FileDescriptor file_descriptor, std::string const& status,
                        std::string const& content_type, std::string const& body);
void send_unauthorized(base::OwnedFileDescriptor const& client);

}  // namespace moe::bridge
