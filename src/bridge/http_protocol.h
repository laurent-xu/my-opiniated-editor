#pragma once

#include <optional>
#include <string>

#include "src/base/file_descriptor.h"

namespace moe::bridge {

struct HttpRequest {
  std::string method;
  std::string path;
  std::string raw_headers;
};

[[nodiscard]] HttpRequest read_http_request(base::FileDescriptor file_descriptor);
[[nodiscard]] std::optional<std::string> header_value(HttpRequest const& request,
                                                      std::string const& header_name);
void send_http_response(base::FileDescriptor file_descriptor, std::string const& status,
                        std::string const& content_type, std::string const& body);

}  // namespace moe::bridge
