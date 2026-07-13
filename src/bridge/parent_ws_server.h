#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

namespace moe::bridge {

struct ServerConfig {
  std::string interface = "127.0.0.1";
  int port = 7682;
  std::filesystem::path parent_binary;
  std::filesystem::path working_directory = std::filesystem::current_path();
  std::string auth_token;
  bool allow_unauthenticated_network = false;
};

void request_server_stop();
void print_usage(std::ostream& output);
[[nodiscard]] ServerConfig parse_args(int argc, char** argv);
void run_server(ServerConfig const& config);

}  // namespace moe::bridge
