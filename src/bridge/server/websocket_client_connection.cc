#include "src/bridge/server/websocket_client_connection.h"

#include <exception>
#include <mutex>
#include <utility>

#include "src/bridge/websocket_protocol.h"

namespace moe::bridge::server {

WebsocketClientConnection::WebsocketClientConnection(base::OwnedFileDescriptor client_descriptor)
    : client(std::move(client_descriptor)) {}

base::FileDescriptor WebsocketClientConnection::file_descriptor() const { return client.get(); }

bool WebsocketClientConnection::send_binary(std::string_view const payload) {
  std::scoped_lock const lock(send_mutex);
  try {
    send_websocket_frame(client.get(), WebsocketFrame::Opcode::BINARY, payload);
    return true;
  } catch (std::exception const&) {
    return false;
  }
}

}  // namespace moe::bridge::server
