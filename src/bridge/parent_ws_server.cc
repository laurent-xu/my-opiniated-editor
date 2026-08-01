#include "src/bridge/parent_ws_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "src/base/owned_file_descriptor.h"
#include "src/bridge/browser_assets.h"
#include "src/bridge/http_protocol.h"
#include "src/bridge/parent_pty_session.h"
#include "src/bridge/pty_size.h"
#include "src/bridge/server/pty_websocket_hub.h"
#include "src/bridge/socket_io.h"
#include "src/bridge/websocket_protocol.h"

namespace moe::bridge {
namespace {

using base::FileDescriptor;
using base::NetworkPort;
using base::OwnedFileDescriptor;

constexpr int DEFAULT_ROWS = 24;
constexpr int DEFAULT_COLS = 80;
constexpr std::chrono::milliseconds POLL_TIMEOUT{250};
constexpr char TRAY_COMMAND_PREFIX = '\x18';
constexpr int MIN_ANONYMOUS_TRAY = 1;
constexpr int MAX_ANONYMOUS_TRAY = 9;
constexpr char const* PARENT_STATE_DIRECTORY_ENVIRONMENT = "MOE_STATE_DIRECTORY";

std::sig_atomic_t volatile keep_running = 1;

struct JsonKey {
  std::string_view value;
};

bool should_keep_running() { return keep_running != 0; }

bool is_loopback_interface(std::string_view const interface) {
  return interface == "127.0.0.1" || interface.starts_with("127.");
}

std::optional<int> parse_json_int(std::string_view json, JsonKey const key) {
  std::string const quoted_key = "\"" + std::string(key.value) + "\"";
  std::size_t const key_position = json.find(quoted_key);
  if (key_position == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t const colon_position = json.find(':', key_position + quoted_key.size());
  if (colon_position == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t value_start = colon_position + 1U;
  while (value_start < json.size() && json[value_start] == ' ') {
    ++value_start;
  }
  int value = 0;
  char const* const begin = json.data() + value_start;
  char const* const end = json.data() + json.size();
  std::from_chars_result const result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{}) {
    return std::nullopt;
  }
  return value;
}

PtySize parse_resize_payload(std::string_view const payload) {
  std::optional<int> const columns = parse_json_int(payload, JsonKey{.value = "columns"});
  std::optional<int> const rows = parse_json_int(payload, JsonKey{.value = "rows"});
  if (!columns.has_value() || !rows.has_value()) {
    throw std::runtime_error("resize payload requires columns and rows");
  }
  return {.rows = *rows, .cols = *columns};
}

int parse_tray_switch_payload(std::string_view const payload) {
  std::optional<int> const tray = parse_json_int(payload, JsonKey{.value = "tray"});
  if (!tray.has_value() || *tray < MIN_ANONYMOUS_TRAY || *tray > MAX_ANONYMOUS_TRAY) {
    throw std::runtime_error("tray switch payload requires tray 1 through 9");
  }
  return *tray;
}

void switch_parent_tray(ParentPtySession const& session, int const tray_number) {
  std::array<char, 2> const command{TRAY_COMMAND_PREFIX, static_cast<char>('0' + tray_number)};
  session.write(std::string_view(command.data(), command.size()));
}

void open_parent_worktree_manager(ParentPtySession const& session) {
  std::array<char, 2> const command{TRAY_COMMAND_PREFIX, 'w'};
  session.write(std::string_view(command.data(), command.size()));
}

void toggle_parent_command_mode(ParentPtySession const& session) {
  std::array<char, 2> const command{TRAY_COMMAND_PREFIX, 'e'};
  session.write(std::string_view(command.data(), command.size()));
}

void send_parent_worktree_picker_command(ParentPtySession const& session,
                                         std::string_view const command) {
  if (command.size() != 1U || (command.front() != 'c' && command.front() != 'r' &&
                               command.front() != 'y' && command.front() != 'n')) {
    throw std::runtime_error("worktree picker command requires c, r, y, or n");
  }
  std::array<char, 2> const parent_command{TRAY_COMMAND_PREFIX, command.front()};
  session.write(std::string_view(parent_command.data(), parent_command.size()));
}

void send_parent_worktree_overlay_navigation(ParentPtySession const& session,
                                             std::string_view const navigation) {
  char parent_command_byte = '\0';
  if (navigation == "up") {
    parent_command_byte = 'A';
  } else if (navigation == "down") {
    parent_command_byte = 'B';
  } else if (navigation == "right") {
    parent_command_byte = 'C';
  } else if (navigation == "left") {
    parent_command_byte = 'D';
  } else if (navigation == "tab") {
    parent_command_byte = 'I';
  } else if (navigation == "backtab") {
    parent_command_byte = 'Z';
  } else if (navigation == "enter") {
    parent_command_byte = 'M';
  } else {
    throw std::runtime_error("worktree overlay navigation is invalid");
  }

  std::array<char, 2> const parent_command{TRAY_COMMAND_PREFIX, parent_command_byte};
  session.write(std::string_view(parent_command.data(), parent_command.size()));
}

void handle_websocket_payload(ParentPtySession const& session, std::string_view const payload) {
  if (payload.empty()) {
    return;
  }

  char const command = payload.front();
  std::string_view const data = payload.substr(1U);
  if (command == '0') {
    session.write(data);
    return;
  }
  if (command == '1') {
    session.resize(parse_resize_payload(data));
    return;
  }
  if (command == '2') {
    switch_parent_tray(session, parse_tray_switch_payload(data));
    return;
  }
  if (command == '3') {
    open_parent_worktree_manager(session);
    return;
  }
  if (command == '5') {
    toggle_parent_command_mode(session);
    return;
  }
  if (command == '6') {
    send_parent_worktree_picker_command(session, data);
    return;
  }
  if (command == '7') {
    send_parent_worktree_overlay_navigation(session, data);
  }
}

void send_health(OwnedFileDescriptor const& client, ParentPtySession const& session) {
  std::string const body =
      R"({"ok":true,"parentPid":)" + std::to_string(session.child_pid().value()) + "}\n";
  send_http_response(client.get(), "200 OK", "application/json", body);
}

void handle_client(OwnedFileDescriptor client, ParentPtySession const& session,
                   ServerConfig const& config, server::PtyWebsocketHub& hub) {
  HttpRequest const request = read_http_request(client.get());
  if (path_requires_authentication(request.path) &&
      !request_has_auth_token(request, config.auth_token)) {
    send_unauthorized(client);
    return;
  }

  if (request.path == "/") {
    send_http_response(client.get(), "200 OK", "text/html; charset=utf-8", browser_html());
    return;
  }
  if (request.path == "/client.js") {
    send_http_response(client.get(), "200 OK", "application/javascript; charset=utf-8",
                       browser_client_js());
    return;
  }
  if (request.path == "/style.css") {
    send_http_response(client.get(), "200 OK", "text/css; charset=utf-8", browser_css());
    return;
  }
  if (request.path == "/health") {
    send_health(client, session);
    return;
  }
  if (request.path != "/ws") {
    send_http_response(client.get(), "404 Not Found", "text/plain", "not found\n");
    return;
  }

  if (send_websocket_handshake(client, request)) {
    hub.serve_client(std::move(client));
  }
}

OwnedFileDescriptor listen_on(std::string const& interface, NetworkPort const port) {
  OwnedFileDescriptor listener(FileDescriptor(::socket(AF_INET, SOCK_STREAM, 0)));
  if (!listener.valid()) {
    throw errno_error("socket failed");
  }

  int const enabled = 1;
  if (setsockopt(listener.get().value(), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) !=
      0) {
    throw errno_error("setsockopt SO_REUSEADDR failed");
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port.value());
  if (inet_pton(AF_INET, interface.c_str(), &address.sin_addr) != 1) {
    throw std::runtime_error("invalid IPv4 interface: " + interface);
  }

  if (bind(listener.get().value(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    throw errno_error("bind failed");
  }
  if (listen(listener.get().value(), 8) != 0) {
    throw errno_error("listen failed");
  }
  return listener;
}

NetworkPort bound_port(OwnedFileDescriptor const& listener) {
  sockaddr_in address{};
  socklen_t length = sizeof(address);
  if (getsockname(listener.get().value(), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    throw errno_error("getsockname failed");
  }
  std::optional<NetworkPort> const port = NetworkPort::from_int(ntohs(address.sin_port));
  if (!port.has_value()) {
    throw std::runtime_error("listener has invalid bound port");
  }
  return *port;
}

OwnedFileDescriptor accept_client(OwnedFileDescriptor const& listener) {
  while (true) {
    int const client = accept(listener.get().value(), nullptr, nullptr);
    if (client >= 0) {
      return OwnedFileDescriptor(FileDescriptor(client));
    }
    if (errno != EINTR) {
      throw errno_error("accept failed");
    }
  }
}

}  // namespace

void request_server_stop() { keep_running = 0; }

void print_usage(std::ostream& output) {
  output << "usage: parent_ws_bridge --parent <path> [--cwd <path>] "
            "--state-directory <path> [--interface <addr>] [--port <port>] [--token <secret>] "
            "[--allow-unauthenticated-network]\n";
}

ServerConfig parse_args(int const argc, char** argv) {
  ServerConfig config;
  for (int index = 1; index < argc; ++index) {
    std::string_view const arg(argv[index]);
    auto require_value = [&](std::string_view const name) -> std::string {
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string(name));
      }
      ++index;
      return argv[index];
    };

    if (arg == "--parent") {
      config.parent_binary = require_value(arg);
    } else if (arg == "--cwd") {
      config.working_directory = require_value(arg);
    } else if (arg == "--state-directory") {
      config.state_directory = require_value(arg);
    } else if (arg == "--interface") {
      config.interface = require_value(arg);
    } else if (arg == "--port") {
      std::string const value = require_value(arg);
      int parsed_port = 0;
      std::from_chars_result const result =
          std::from_chars(value.data(), value.data() + value.size(), parsed_port);
      std::optional<NetworkPort> const port = NetworkPort::from_int(parsed_port);
      if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
          !port.has_value()) {
        throw std::runtime_error("invalid port: " + value);
      }
      config.port = *port;
    } else if (arg == "--token") {
      config.auth_token = require_value(arg);
      if (config.auth_token.empty()) {
        throw std::runtime_error("--token must not be empty");
      }
    } else if (arg == "--allow-unauthenticated-network") {
      config.allow_unauthenticated_network = true;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(std::cout);
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(arg));
    }
  }
  return config;
}

void run_server(ServerConfig const& config) {
  if (config.parent_binary.empty()) {
    throw std::runtime_error("--parent is required");
  }
  if (config.state_directory.empty()) {
    throw std::runtime_error("--state-directory is required");
  }
  if (!config.state_directory.is_absolute()) {
    throw std::runtime_error("--state-directory must be absolute");
  }
  if (!is_loopback_interface(config.interface) && config.auth_token.empty() &&
      !config.allow_unauthenticated_network) {
    throw std::runtime_error(
        "network bind requires --token <secret> or --allow-unauthenticated-network");
  }
  if (::setenv(PARENT_STATE_DIRECTORY_ENVIRONMENT, config.state_directory.c_str(), 1) != 0) {
    throw errno_error("set parent state directory failed");
  }

  std::vector<std::string> const command{config.parent_binary.string()};
  std::unique_ptr<ParentPtySession> session = ParentPtySession::start(
      command, config.working_directory, PtySize{.rows = DEFAULT_ROWS, .cols = DEFAULT_COLS});

  OwnedFileDescriptor const listener = listen_on(config.interface, config.port);
  std::cout << "parent-ws-bridge listening interface=" << config.interface << " port="
            << bound_port(listener).value() << " parent_pid=" << session->child_pid().value()
            << '\n';
  std::cout.flush();

  server::PtyWebsocketHub hub(*session, POLL_TIMEOUT, should_keep_running,
                              [parent_session = session.get()](std::string_view const payload) {
                                handle_websocket_payload(*parent_session, payload);
                              });
  while (should_keep_running()) {
    pollfd descriptor{.fd = listener.get().value(), .events = POLLIN, .revents = 0};
    int const result = poll(&descriptor, 1, static_cast<int>(POLL_TIMEOUT.count()));
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw errno_error("poll listener failed");
    }
    if (result == 0) {
      continue;
    }

    OwnedFileDescriptor client = accept_client(listener);
    std::thread([client = std::move(client), &config, session = session.get(), &hub]() mutable {
      try {
        handle_client(std::move(client), *session, config, hub);
      } catch (std::exception const& error) {
        std::cerr << "client error: " << error.what() << '\n';
      }
    }).detach();
  }
}

}  // namespace moe::bridge
