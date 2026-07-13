#include "src/bridge/browser_assets.h"

namespace moe::bridge {

std::string browser_html() {
  return R"HTML(<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>my-opiniated-editor</title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@xterm/xterm/css/xterm.css">
    <link rel="stylesheet" href="/style.css">
  </head>
  <body>
    <main id="workspace">
      <div id="terminal" aria-label="workspace terminal"></div>
      <div id="status" aria-live="polite">connecting</div>
    </main>
    <script src="https://cdn.jsdelivr.net/npm/@xterm/xterm/lib/xterm.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/@xterm/addon-fit/lib/addon-fit.js"></script>
    <script src="/client.js"></script>
  </body>
</html>
)HTML";
}

std::string browser_css() {
  return R"CSS(:root {
  color-scheme: dark;
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
}

html,
body,
#workspace {
  height: 100%;
  margin: 0;
}

body {
  background: #0b0d0e;
  color: #d9e2df;
}

#workspace {
  display: grid;
  grid-template-rows: 1fr 24px;
}

#terminal {
  min-height: 0;
}

#status {
  align-content: center;
  border-top: 1px solid #27302d;
  color: #9fb2ab;
  font-size: 12px;
  padding: 0 10px;
}
)CSS";
}

std::string browser_client_js() {
  return R"JS((() => {
  const terminalElement = document.getElementById("terminal");
  const statusElement = document.getElementById("status");
  const token = new URLSearchParams(window.location.search).get("token") || "";
  const websocketPath = token ? `/ws?token=${encodeURIComponent(token)}` : "/ws";
  const terminal = new Terminal({
    cursorBlink: true,
    convertEol: true,
    fontFamily: "ui-monospace, SFMono-Regular, Menlo, Consolas, monospace",
    fontSize: 14,
    theme: {
      background: "#0b0d0e",
      foreground: "#d9e2df",
      cursor: "#f5d06f"
    }
  });
  const fitAddon = new FitAddon.FitAddon();
  terminal.loadAddon(fitAddon);
  terminal.open(terminalElement);

  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const socket = new WebSocket(`${protocol}//${window.location.host}${websocketPath}`, "workspace-pty");
  socket.binaryType = "arraybuffer";

  function setStatus(text) {
    statusElement.textContent = text;
  }

  function sendCommand(command, payload) {
    if (socket.readyState !== WebSocket.OPEN) {
      return;
    }
    const body = encoder.encode(`${command}${payload}`);
    socket.send(body);
  }

  function fitAndSendSize() {
    fitAddon.fit();
    sendCommand("1", JSON.stringify({ columns: terminal.cols, rows: terminal.rows }));
  }

  terminal.onData((data) => {
    sendCommand("0", data);
  });

  socket.addEventListener("open", () => {
    setStatus("connected");
    fitAndSendSize();
    terminal.focus();
  });

  socket.addEventListener("message", (event) => {
    const bytes = event.data instanceof ArrayBuffer ? new Uint8Array(event.data) : encoder.encode(event.data);
    if (bytes.length === 0) {
      return;
    }
    const command = String.fromCharCode(bytes[0]);
    const payload = decoder.decode(bytes.slice(1));
    if (command === "0") {
      terminal.write(payload);
    }
  });

  socket.addEventListener("close", () => {
    setStatus("disconnected");
  });

  window.addEventListener("resize", () => {
    fitAndSendSize();
  });
})();
)JS";
}

}  // namespace moe::bridge
