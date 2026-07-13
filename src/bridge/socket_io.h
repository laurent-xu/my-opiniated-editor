#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "src/base/file_descriptor.h"

namespace moe::bridge {

struct ByteCount {
  std::size_t value;
};

[[nodiscard]] std::runtime_error errno_error(std::string const& action);
void send_all(base::FileDescriptor file_descriptor, std::string_view bytes);
[[nodiscard]] std::optional<std::string> read_exact_or_closed(base::FileDescriptor file_descriptor,
                                                              ByteCount byte_count);

}  // namespace moe::bridge
