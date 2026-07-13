#include <csignal>
#include <exception>
#include <iostream>

#include "src/bridge/parent_ws_server.h"

namespace {

void handle_signal(int const signal_number) {
  static_cast<void>(signal_number);
  moe::bridge::request_server_stop();
}

}  // namespace

int main(int const argc, char** argv) {
  std::signal(SIGTERM, handle_signal);
  std::signal(SIGINT, handle_signal);
  std::signal(SIGPIPE, SIG_IGN);

  try {
    moe::bridge::run_server(moe::bridge::parse_args(argc, argv));
    return 0;
  } catch (std::exception const& error) {
    std::cerr << "parent_ws_bridge: " << error.what() << '\n';
    moe::bridge::print_usage(std::cerr);
    return 1;
  }
}
