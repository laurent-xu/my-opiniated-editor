#pragma once

#include <mutex>
#include <string_view>

#include "src/base/file_descriptor.h"
#include "src/base/owned_file_descriptor.h"

namespace moe::bridge::server {

class WebsocketClientConnection {
 public:
  explicit WebsocketClientConnection(base::OwnedFileDescriptor client_descriptor);

  [[nodiscard]] base::FileDescriptor file_descriptor() const;
  [[nodiscard]] bool send_binary(std::string_view payload);

 private:
  base::OwnedFileDescriptor client;
  std::mutex send_mutex;
};

}  // namespace moe::bridge::server
