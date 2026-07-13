#include "src/bridge/socket_io.h"

#include <sys/socket.h>

#include <cerrno>
#include <cstring>

namespace moe::bridge {

std::runtime_error errno_error(std::string const& action) {
  return std::runtime_error(action + ": " + std::strerror(errno));
}

void send_all(base::FileDescriptor const file_descriptor, std::string_view bytes) {
  while (!bytes.empty()) {
    ssize_t const sent = ::send(file_descriptor.value(), bytes.data(), bytes.size(), MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw errno_error("send failed");
    }
    bytes.remove_prefix(static_cast<std::size_t>(sent));
  }
}

std::optional<std::string> read_exact_or_closed(base::FileDescriptor const file_descriptor,
                                                ByteCount const byte_count) {
  std::string output(byte_count.value, '\0');
  std::size_t offset = 0;
  while (offset < byte_count.value) {
    ssize_t const received =
        ::recv(file_descriptor.value(), output.data() + offset, output.size() - offset, 0);
    if (received == 0) {
      return std::nullopt;
    }
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == ECONNRESET) {
        return std::nullopt;
      }
      throw errno_error("recv failed");
    }
    offset += static_cast<std::size_t>(received);
  }
  return output;
}

}  // namespace moe::bridge
