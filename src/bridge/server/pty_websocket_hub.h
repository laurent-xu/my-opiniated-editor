#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace moe::base {
class OwnedFileDescriptor;
}

namespace moe::bridge {
class ParentPtySession;
}

namespace moe::bridge::server {

class WebsocketClientConnection;

class PtyWebsocketHub {
 public:
  using ContinuePredicate = std::function<bool()>;
  using ClientPayloadHandler = std::function<void(std::string_view)>;

  PtyWebsocketHub(ParentPtySession const& parent_session, std::chrono::milliseconds poll_timeout,
                  ContinuePredicate continue_predicate,
                  ClientPayloadHandler client_payload_handler);

  PtyWebsocketHub(PtyWebsocketHub const&) = delete;
  PtyWebsocketHub& operator=(PtyWebsocketHub const&) = delete;

  ~PtyWebsocketHub();

  void serve_client(base::OwnedFileDescriptor client_descriptor);

 private:
  static constexpr std::size_t MAX_TERMINAL_BACKLOG = static_cast<std::size_t>(64U) * 1024U;
  static constexpr std::size_t MAX_PARENT_STATUS_BUFFER = static_cast<std::size_t>(64U) * 1024U;

  void add_client(std::shared_ptr<WebsocketClientConnection> const& client);
  void remove_client(std::shared_ptr<WebsocketClientConnection> const& client);
  void broadcast_terminal_output(std::string const& terminal_output);
  void broadcast_parent_status(std::string status);
  void send_to_clients(std::string_view payload);
  void read_terminal_output();
  void read_parent_status();
  void read_pty_loop();

  ParentPtySession const& session;
  std::chrono::milliseconds poll_timeout;
  ContinuePredicate continue_predicate;
  ClientPayloadHandler client_payload_handler;
  std::atomic<bool> stopping{false};
  std::thread pty_reader;
  std::mutex parent_input_mutex;
  std::mutex state_mutex;
  std::vector<std::shared_ptr<WebsocketClientConnection>> clients;
  std::string terminal_backlog;
  std::string latest_parent_status;
  std::string parent_status_buffer;
};

}  // namespace moe::bridge::server
