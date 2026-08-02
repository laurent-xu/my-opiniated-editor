#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "src/base/network_port.h"

namespace moe::bridge {

struct ServerConfig {
  static constexpr base::NetworkPort DEFAULT_PORT = base::NetworkPort::from_int(7682).value();

  std::string interface = "127.0.0.1";
  base::NetworkPort port = DEFAULT_PORT;
  std::filesystem::path parent_binary;
  std::filesystem::path working_directory = std::filesystem::current_path();
  std::filesystem::path state_directory;
};

void request_server_stop();
void print_usage(std::ostream& output);
[[nodiscard]] ServerConfig parse_args(int argc, char** argv);
void run_server(ServerConfig const& config);

}  // namespace moe::bridge
