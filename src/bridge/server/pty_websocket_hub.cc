#include "src/bridge/server/pty_websocket_hub.h"

#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <optional>
#include <utility>

#include "src/base/owned_file_descriptor.h"
#include "src/bridge/parent_pty_session.h"
#include "src/bridge/protocol/application_message_codec.h"
#include "src/bridge/protocol/bridge_to_browser_message.h"
#include "src/bridge/server/websocket_client_connection.h"
#include "src/bridge/socket_io.h"
#include "src/bridge/websocket_protocol.h"

namespace moe::bridge::server {

PtyWebsocketHub::PtyWebsocketHub(ParentPtySession const& parent_session,
                                 std::chrono::milliseconds const poll_timeout,
                                 ContinuePredicate continue_predicate,
                                 ClientPayloadHandler client_payload_handler)
    : session(parent_session),
      poll_timeout(poll_timeout),
      continue_predicate(std::move(continue_predicate)),
      client_payload_handler(std::move(client_payload_handler)),
      pty_reader([this] { read_pty_loop(); }) {}

PtyWebsocketHub::~PtyWebsocketHub() {
  stopping = true;
  if (pty_reader.joinable()) {
    pty_reader.join();
  }
}

void PtyWebsocketHub::serve_client(base::OwnedFileDescriptor client_descriptor) {
  auto client = std::make_shared<WebsocketClientConnection>(std::move(client_descriptor));
  add_client(client);

  while (continue_predicate()) {
    pollfd descriptor{.fd = client->file_descriptor().value(), .events = POLLIN, .revents = 0};
    int const result = poll(&descriptor, 1, static_cast<int>(poll_timeout.count()));
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
      client_payload_handler(frame->payload);
    }
  }

  remove_client(client);
}

void PtyWebsocketHub::add_client(std::shared_ptr<WebsocketClientConnection> const& client) {
  std::scoped_lock const lock(state_mutex);
  clients.push_back(client);
  if (!terminal_backlog.empty()) {
    std::string const payload = protocol::encode_bridge_to_browser_message(
        {.type = protocol::BridgeToBrowserMessage::Type::TERMINAL_OUTPUT,
         .payload = terminal_backlog});
    static_cast<void>(client->send_binary(payload));
  }
  if (!latest_parent_status.empty()) {
    std::string const payload = protocol::encode_bridge_to_browser_message(
        {.type = protocol::BridgeToBrowserMessage::Type::PARENT_STATUS,
         .payload = latest_parent_status});
    static_cast<void>(client->send_binary(payload));
  }
}

void PtyWebsocketHub::remove_client(std::shared_ptr<WebsocketClientConnection> const& client) {
  std::scoped_lock const lock(state_mutex);
  for (auto iterator = clients.begin(); iterator != clients.end();) {
    if (*iterator == client) {
      iterator = clients.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

void PtyWebsocketHub::broadcast_terminal_output(std::string const& terminal_output) {
  std::scoped_lock const lock(state_mutex);
  terminal_backlog.append(terminal_output);
  if (terminal_backlog.size() > MAX_TERMINAL_BACKLOG) {
    terminal_backlog.erase(0, terminal_backlog.size() - MAX_TERMINAL_BACKLOG);
  }

  std::string const payload = protocol::encode_bridge_to_browser_message(
      {.type = protocol::BridgeToBrowserMessage::Type::TERMINAL_OUTPUT,
       .payload = terminal_output});
  send_to_clients(payload);
}

void PtyWebsocketHub::broadcast_parent_status(std::string status) {
  std::scoped_lock const lock(state_mutex);
  latest_parent_status = std::move(status);

  std::string const payload = protocol::encode_bridge_to_browser_message(
      {.type = protocol::BridgeToBrowserMessage::Type::PARENT_STATUS,
       .payload = latest_parent_status});
  send_to_clients(payload);
}

void PtyWebsocketHub::send_to_clients(std::string_view const payload) {
  for (auto iterator = clients.begin(); iterator != clients.end();) {
    if ((*iterator)->send_binary(payload)) {
      ++iterator;
    } else {
      iterator = clients.erase(iterator);
    }
  }
}

void PtyWebsocketHub::read_terminal_output() {
  std::array<char, 4096> buffer{};
  ssize_t const read_count =
      ::read(session.file_descriptor().value(), buffer.data(), buffer.size());
  if (read_count > 0) {
    broadcast_terminal_output(std::string(buffer.data(), static_cast<std::size_t>(read_count)));
  }
}

void PtyWebsocketHub::read_parent_status() {
  std::array<char, 4096> buffer{};
  ssize_t const read_count =
      ::read(session.status_file_descriptor().value(), buffer.data(), buffer.size());
  if (read_count <= 0) {
    return;
  }

  parent_status_buffer.append(buffer.data(), static_cast<std::size_t>(read_count));
  for (std::size_t newline = parent_status_buffer.find('\n'); newline != std::string::npos;
       newline = parent_status_buffer.find('\n')) {
    std::string status = parent_status_buffer.substr(0, newline);
    parent_status_buffer.erase(0, newline + 1U);
    if (!status.empty()) {
      broadcast_parent_status(std::move(status));
    }
  }
  if (parent_status_buffer.size() > MAX_PARENT_STATUS_BUFFER) {
    std::cerr << "parent status message exceeded buffer limit\n";
    parent_status_buffer.clear();
  }
}

void PtyWebsocketHub::read_pty_loop() {
  while (continue_predicate() && !stopping) {
    std::array<pollfd, 2> descriptors{
        pollfd{.fd = session.file_descriptor().value(), .events = POLLIN, .revents = 0},
        pollfd{.fd = session.status_file_descriptor().value(), .events = POLLIN, .revents = 0},
    };
    int const result = poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()),
                            static_cast<int>(poll_timeout.count()));
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "pty reader error: " << std::strerror(errno) << '\n';
      return;
    }
    if (result == 0) {
      continue;
    }
    if ((descriptors[0].revents & POLLIN) != 0) {
      read_terminal_output();
    }
    if ((descriptors[1].revents & POLLIN) != 0) {
      read_parent_status();
    }
  }
}

}  // namespace moe::bridge::server
