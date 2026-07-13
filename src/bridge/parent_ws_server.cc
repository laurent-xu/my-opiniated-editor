#include "src/bridge/parent_ws_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include "src/base/owned_file_descriptor.h"
#include "src/bridge/browser_assets.h"
#include "src/bridge/http_protocol.h"
#include "src/bridge/parent_pty_session.h"
#include "src/bridge/pty_size.h"
#include "src/bridge/socket_io.h"
#include "src/bridge/websocket_protocol.h"

namespace moe::bridge {
namespace {

using base::FileDescriptor;
using base::OwnedFileDescriptor;

constexpr int DEFAULT_ROWS = 24;
constexpr int DEFAULT_COLS = 80;
constexpr int POLL_TIMEOUT_MILLISECONDS = 250;

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
  }
}

void send_health(OwnedFileDescriptor const& client, ParentPtySession const& session) {
  std::string const body =
      R"({"ok":true,"parentPid":)" + std::to_string(session.child_pid().value()) + "}\n";
  send_http_response(client.get(), "200 OK", "application/json", body);
}

class WebsocketClientConnection {
 public:
  explicit WebsocketClientConnection(OwnedFileDescriptor client_descriptor)
      : client(std::move(client_descriptor)) {}

  [[nodiscard]] FileDescriptor file_descriptor() const { return client.get(); }

  [[nodiscard]] bool send_binary(std::string_view payload) {
    std::scoped_lock const lock(send_mutex);
    try {
      send_websocket_frame(client.get(), WebsocketFrame::Opcode::BINARY, payload);
      return true;
    } catch (std::exception const&) {
      return false;
    }
  }

 private:
  OwnedFileDescriptor client;
  std::mutex send_mutex;
};

class PtyWebsocketHub {
 public:
  explicit PtyWebsocketHub(ParentPtySession const& parent_session)
      : session(parent_session), pty_reader([this] { read_pty_loop(); }) {}

  PtyWebsocketHub(PtyWebsocketHub const&) = delete;
  PtyWebsocketHub& operator=(PtyWebsocketHub const&) = delete;

  ~PtyWebsocketHub() {
    stopping = true;
    if (pty_reader.joinable()) {
      pty_reader.join();
    }
  }

  void serve_client(OwnedFileDescriptor client_descriptor) {
    auto client = std::make_shared<WebsocketClientConnection>(std::move(client_descriptor));
    add_client(client);

    while (should_keep_running()) {
      pollfd descriptor{.fd = client->file_descriptor().value(), .events = POLLIN, .revents = 0};
      int const result = poll(&descriptor, 1, POLL_TIMEOUT_MILLISECONDS);
      if (result < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw errno_error("poll websocket client failed");
      }
      if (result == 0) {
        continue;
      }
      if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        continue;
      }

      std::optional<WebsocketFrame> frame = read_websocket_frame(client->file_descriptor());
      if (!frame.has_value() || frame->opcode == WebsocketFrame::Opcode::CLOSE) {
        break;
      }
      if (frame->opcode == WebsocketFrame::Opcode::BINARY ||
          frame->opcode == WebsocketFrame::Opcode::TEXT) {
        std::scoped_lock const lock(parent_input_mutex);
        handle_websocket_payload(session, frame->payload);
      }
    }

    remove_client(client);
  }

 private:
  static constexpr std::size_t MAX_TERMINAL_BACKLOG = static_cast<std::size_t>(64U) * 1024U;

  void add_client(std::shared_ptr<WebsocketClientConnection> const& client) {
    std::scoped_lock const lock(state_mutex);
    clients.push_back(client);
    if (!terminal_backlog.empty()) {
      std::string payload("0");
      payload.append(terminal_backlog);
      static_cast<void>(client->send_binary(payload));
    }
  }

  void remove_client(std::shared_ptr<WebsocketClientConnection> const& client) {
    std::scoped_lock const lock(state_mutex);
    for (auto iterator = clients.begin(); iterator != clients.end();) {
      if (*iterator == client) {
        iterator = clients.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }

  void broadcast_terminal_output(std::string const& terminal_output) {
    std::scoped_lock const lock(state_mutex);
    terminal_backlog.append(terminal_output);
    if (terminal_backlog.size() > MAX_TERMINAL_BACKLOG) {
      terminal_backlog.erase(0, terminal_backlog.size() - MAX_TERMINAL_BACKLOG);
    }

    std::string payload("0");
    payload.append(terminal_output);
    send_to_clients(payload);
  }

  void send_to_clients(std::string_view payload) {
    for (auto iterator = clients.begin(); iterator != clients.end();) {
      if ((*iterator)->send_binary(payload)) {
        ++iterator;
      } else {
        iterator = clients.erase(iterator);
      }
    }
  }

  void read_pty_loop() {
    while (should_keep_running() && !stopping) {
      pollfd descriptor{.fd = session.file_descriptor().value(), .events = POLLIN, .revents = 0};
      int const result = poll(&descriptor, 1, POLL_TIMEOUT_MILLISECONDS);
      if (result < 0) {
        if (errno == EINTR) {
          continue;
        }
        std::cerr << "pty reader error: " << std::strerror(errno) << '\n';
        return;
      }
      if (result == 0 || (descriptor.revents & POLLIN) == 0) {
        continue;
      }

      std::array<char, 4096> buffer{};
      ssize_t const read_count =
          ::read(session.file_descriptor().value(), buffer.data(), buffer.size());
      if (read_count <= 0) {
        continue;
      }

      broadcast_terminal_output(std::string(buffer.data(), static_cast<std::size_t>(read_count)));
    }
  }

  ParentPtySession const& session;
  std::atomic<bool> stopping{false};
  std::thread pty_reader;
  std::mutex parent_input_mutex;
  std::mutex state_mutex;
  std::vector<std::shared_ptr<WebsocketClientConnection>> clients;
  std::string terminal_backlog;
};

void serve_websocket_client(OwnedFileDescriptor client, PtyWebsocketHub& hub) {
  hub.serve_client(std::move(client));
}

void handle_client(OwnedFileDescriptor client, ParentPtySession const& session,
                   ServerConfig const& config, PtyWebsocketHub& hub) {
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
    serve_websocket_client(std::move(client), hub);
  }
}

OwnedFileDescriptor listen_on(std::string const& interface, int const port) {
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
  address.sin_port = htons(static_cast<std::uint16_t>(port));
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

int bound_port(OwnedFileDescriptor const& listener) {
  sockaddr_in address{};
  socklen_t length = sizeof(address);
  if (getsockname(listener.get().value(), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    throw errno_error("getsockname failed");
  }
  return ntohs(address.sin_port);
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
            "[--interface <addr>] [--port <port>] [--token <secret>] "
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
    } else if (arg == "--interface") {
      config.interface = require_value(arg);
    } else if (arg == "--port") {
      std::string const value = require_value(arg);
      int parsed_port = 0;
      std::from_chars_result const result =
          std::from_chars(value.data(), value.data() + value.size(), parsed_port);
      if (result.ec != std::errc{}) {
        throw std::runtime_error("invalid port: " + value);
      }
      config.port = parsed_port;
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
  if (!is_loopback_interface(config.interface) && config.auth_token.empty() &&
      !config.allow_unauthenticated_network) {
    throw std::runtime_error(
        "network bind requires --token <secret> or --allow-unauthenticated-network");
  }

  std::vector<std::string> const command{config.parent_binary.string()};
  std::unique_ptr<ParentPtySession> session = ParentPtySession::start(
      command, config.working_directory, PtySize{.rows = DEFAULT_ROWS, .cols = DEFAULT_COLS});

  OwnedFileDescriptor const listener = listen_on(config.interface, config.port);
  std::cout << "parent-ws-bridge listening interface=" << config.interface << " port="
            << bound_port(listener) << " parent_pid=" << session->child_pid().value() << '\n';
  std::cout.flush();

  PtyWebsocketHub hub(*session);
  while (should_keep_running()) {
    pollfd descriptor{.fd = listener.get().value(), .events = POLLIN, .revents = 0};
    int const result = poll(&descriptor, 1, POLL_TIMEOUT_MILLISECONDS);
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
