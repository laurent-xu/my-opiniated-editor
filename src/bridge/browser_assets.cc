#include "src/bridge/browser_assets.h"

namespace moe::bridge {
namespace {

constexpr char const* XTERM_VERSION = "6.0.0";
constexpr char const* XTERM_ADDON_FIT_VERSION = "0.11.0";
constexpr char const* TERMINAL_FONT_FAMILY =
    "\"DejaVu Sans Mono\", \"Liberation Mono\", \"Noto Sans Mono\", "
    "\"Cascadia Mono\", \"JetBrains Mono\", monospace";

}  // namespace

std::string browser_html() {
  return std::string(R"HTML(<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>my-opiniated-editor</title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@xterm/xterm@)HTML") +
         XTERM_VERSION + R"HTML(/css/xterm.css">
    <link rel="stylesheet" href="/style.css">
  </head>
  <body>
    <main id="workspace">
      <div id="terminal" aria-label="workspace terminal"></div>
      <div id="status" aria-live="polite">connecting</div>
    </main>
    <script src="https://cdn.jsdelivr.net/npm/@xterm/xterm@)HTML" +
         XTERM_VERSION +
         R"HTML(/lib/xterm.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/@xterm/addon-fit@)HTML" +
         XTERM_ADDON_FIT_VERSION + R"HTML(/lib/addon-fit.js"></script>
    <script src="/client.js"></script>
  </body>
</html>
)HTML";
}

std::string browser_css() {
  return R"CSS(:root {
  color-scheme: dark;
  font-family: "DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono", "Cascadia Mono",
    "JetBrains Mono", monospace;
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
  return std::string(R"JS((() => {
  const terminalElement = document.getElementById("terminal");
  const statusElement = document.getElementById("status");
  const token = new URLSearchParams(window.location.search).get("token") || "";
  const websocketPath = token ? `/ws?token=${encodeURIComponent(token)}` : "/ws";
  const terminal = new Terminal({
    cursorBlink: true,
    convertEol: true,
    fontFamily: ')JS") +
         TERMINAL_FONT_FAMILY + R"JS(',
    fontSize: 14,
    lineHeight: 1.15,
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
  let connectionState = "connecting";
  let activeTrayNumber = 1;
  let commandMode = false;
  let statusNote = "";

  function renderStatus() {
    const parts = [connectionState, `tray ${activeTrayNumber}`];
    if (commandMode) {
      parts.push("command");
    }
    if (statusNote) {
      parts.push(statusNote);
    }
    statusElement.textContent = parts.join(" | ");
  }

  function setConnectionState(text) {
    connectionState = text;
    renderStatus();
  }

  function setCommandMode(enabled) {
    commandMode = enabled;
    if (enabled) {
      statusNote = "";
    }
    renderStatus();
  }

  function sendCommand(command, payload) {
    if (socket.readyState !== WebSocket.OPEN) {
      return;
    }
    const body = encoder.encode(`${command}${payload}`);
    socket.send(body);
  }

  function sendTraySwitch(trayNumber) {
    activeTrayNumber = trayNumber;
    statusNote = "";
    renderStatus();
    sendCommand("2", JSON.stringify({ tray: trayNumber }));
  }

  function showTrayFindPlaceholder() {
    statusNote = "tray find not implemented";
    renderStatus();
  }

  function fitAndSendSize() {
    fitAddon.fit();
    sendCommand("1", JSON.stringify({ columns: terminal.cols, rows: terminal.rows }));
  }

  function terminalShouldReceiveKey() {
    const activeElement = document.activeElement;
    return activeElement === document.body || activeElement === terminalElement || terminalElement.contains(activeElement);
  }

  function isTabKey(event) {
    return event.key === "Tab" || event.code === "Tab" || event.keyCode === 9;
  }

  function isEscapeKey(event) {
    return event.key === "Escape" || event.code === "Escape" || event.keyCode === 27;
  }

  function isModifierOnlyKey(event) {
    return event.key === "Shift" || event.key === "Control" || event.key === "Alt" ||
      event.key === "Meta" || event.code === "ShiftLeft" || event.code === "ShiftRight" ||
      event.code === "ControlLeft" || event.code === "ControlRight" ||
      event.code === "AltLeft" || event.code === "AltRight" ||
      event.code === "MetaLeft" || event.code === "MetaRight";
  }

  function trayNumberFromShiftDigit(event) {
    if (!event.shiftKey || event.altKey || event.ctrlKey || event.metaKey) {
      return null;
    }
    const codeMatch = /^Digit([1-9])$/.exec(event.code || "");
    if (codeMatch) {
      return Number(codeMatch[1]);
    }
    if (/^[1-9]$/.test(event.key || "")) {
      return Number(event.key);
    }
    return null;
  }

  function isShiftTKey(event) {
    return event.shiftKey && !event.altKey && !event.ctrlKey && !event.metaKey &&
      (event.code === "KeyT" || event.key === "T");
  }

  function handleTabKey(event) {
    if (isTabKey(event) && !event.altKey && !event.ctrlKey && !event.metaKey) {
      event.preventDefault();
      sendCommand("0", event.shiftKey ? "\x1b[Z" : "\t");
      return true;
    }
    return false;
  }

  function handleCommandModeKey(event) {
    if (isEscapeKey(event)) {
      event.preventDefault();
      if (commandMode) {
        sendCommand("0", "\x1b");
        setCommandMode(false);
      } else {
        setCommandMode(true);
      }
      return true;
    }

    if (!commandMode) {
      return false;
    }

    if (isModifierOnlyKey(event)) {
      event.preventDefault();
      return true;
    }

    event.preventDefault();
    const trayNumber = trayNumberFromShiftDigit(event);
    if (trayNumber !== null) {
      sendTraySwitch(trayNumber);
      return true;
    }
    if (isShiftTKey(event)) {
      showTrayFindPlaceholder();
      return true;
    }

    renderStatus();
    return true;
  }

  function handleTerminalKey(event) {
    return handleCommandModeKey(event) || handleTabKey(event);
  }

  document.addEventListener("keydown", (event) => {
    if (terminalShouldReceiveKey() && handleTerminalKey(event)) {
      event.stopImmediatePropagation();
    }
  }, true);

  terminal.attachCustomKeyEventHandler((event) => {
    if (event.type === "keydown" && handleTerminalKey(event)) {
      return false;
    }
    return true;
  });

  terminal.onData((data) => {
    sendCommand("0", data);
  });

  socket.addEventListener("open", () => {
    setConnectionState("connected");
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
    setConnectionState("disconnected");
  });

  window.addEventListener("resize", () => {
    fitAndSendSize();
  });

  renderStatus();
})();
)JS";
}

}  // namespace moe::bridge
