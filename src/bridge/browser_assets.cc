#include "src/bridge/browser_assets.h"

namespace moe::bridge {
namespace {

constexpr char const* XTERM_VERSION = "6.0.0";
constexpr char const* XTERM_ADDON_FIT_VERSION = "0.11.0";
constexpr char const* XTERM_ADDON_WEBGL_VERSION = "0.19.0";
constexpr char const* TERMINAL_PRIMARY_FONT_FAMILY = "\"JetBrainsMono Nerd Font Mono\"";
constexpr char const* TERMINAL_FONT_FAMILY =
    "\"JetBrainsMono Nerd Font Mono\", \"JetBrainsMono NFM\", "
    "\"JetBrainsMonoNL Nerd Font Mono\", \"JetBrainsMonoNL NFM\", "
    "\"MesloLGS NF\", \"FiraCode Nerd Font Mono\", \"Hack Nerd Font Mono\", "
    "\"CaskaydiaCove Nerd Font Mono\", \"Symbols Nerd Font Mono\", "
    "\"PowerlineSymbols\", \"DejaVu Sans Mono for Powerline\", "
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
    <script src="https://cdn.jsdelivr.net/npm/@xterm/addon-webgl@)HTML" +
         XTERM_ADDON_WEBGL_VERSION + R"HTML(/lib/addon-webgl.js"></script>
    <script src="/client.js"></script>
  </body>
</html>
)HTML";
}

std::string browser_css() {
  return std::string(R"CSS(:root {
  color-scheme: dark;
  font-family: )CSS") +
         TERMINAL_FONT_FAMILY + R"CSS(;
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
  return std::string(R"JS((async () => {
  const terminalElement = document.getElementById("terminal");
  const statusElement = document.getElementById("status");
  const token = new URLSearchParams(window.location.search).get("token") || "";
  const websocketPath = token ? `/ws?token=${encodeURIComponent(token)}` : "/ws";

  async function awaitTerminalFont() {
    if (!document.fonts || !document.fonts.load) {
      return;
    }
    try {
      await document.fonts.load('14px )JS") +
         TERMINAL_PRIMARY_FONT_FAMILY + R"JS(', "\ue0b0");
      await document.fonts.ready;
    } catch (error) {
      return;
    }
  }

  await awaitTerminalFont();

  const terminal = new Terminal({
    cursorBlink: true,
    convertEol: true,
    customGlyphs: true,
    fontFamily: ')JS" +
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
  try {
    const webglAddon = new WebglAddon.WebglAddon();
    terminal.loadAddon(webglAddon);
  } catch (error) {
    console.warn("WebGL terminal renderer unavailable", error);
  }

  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  const statusDecoder = new TextDecoder();
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const socket = new WebSocket(`${protocol}//${window.location.host}${websocketPath}`, "workspace-pty");
  socket.binaryType = "arraybuffer";
  let connectionState = "connecting";
  let activeTrayLabel = "tray 1";
  let commandMode = false;

  function renderStatus() {
    const parts = [connectionState, activeTrayLabel];
    if (commandMode) {
      parts.push("command");
    }
    statusElement.textContent = parts.join(" | ");
  }

  function setConnectionState(text) {
    connectionState = text;
    renderStatus();
  }

  function applyParentStatus(status) {
    if (status.type !== "parent.status") {
      return;
    }
    commandMode = status.commandMode === true;
    if (typeof status.trayLabel === "string" && status.trayLabel.length > 0) {
      activeTrayLabel = status.trayLabel;
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
    sendCommand("2", JSON.stringify({ tray: trayNumber }));
  }

  function toggleWorktreeManager() {
    sendCommand("3", "");
  }

  function sendWorktreePickerCommand(command) {
    sendCommand("6", command);
  }

  function sendWorktreeOverlayNavigation(navigation) {
    sendCommand("7", navigation);
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

  function isShiftWKey(event) {
    return event.shiftKey && !event.altKey && !event.ctrlKey && !event.metaKey &&
      (event.code === "KeyW" || event.key === "W");
  }

  function isShiftCKey(event) {
    return event.shiftKey && !event.altKey && !event.ctrlKey && !event.metaKey &&
      (event.code === "KeyC" || event.key === "C");
  }

  function isShiftRKey(event) {
    return event.shiftKey && !event.altKey && !event.ctrlKey && !event.metaKey &&
      (event.code === "KeyR" || event.key === "R");
  }

  function trayActionConfirmationFromKey(event) {
    if (event.altKey || event.ctrlKey || event.metaKey) {
      return null;
    }
    const key = event.key || "";
    if (key.toLowerCase() === "y" || key.toLowerCase() === "n") {
      return key.toLowerCase();
    }
    return null;
  }

  function worktreeOverlayNavigationFromKey(event) {
    if (event.altKey || event.ctrlKey || event.metaKey) {
      return null;
    }
    if (event.key === "Enter" || event.code === "Enter") {
      return "enter";
    }
    if (isTabKey(event)) {
      return event.shiftKey ? "backtab" : "tab";
    }
    const navigationByKey = {
      ArrowUp: "up",
      ArrowDown: "down",
      ArrowLeft: "left",
      ArrowRight: "right",
    };
    return navigationByKey[event.key] || navigationByKey[event.code] || null;
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
      sendCommand("5", "");
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
    if (isShiftWKey(event)) {
      toggleWorktreeManager();
      return true;
    }
    if (isShiftCKey(event)) {
      sendWorktreePickerCommand("c");
      return true;
    }
    if (isShiftRKey(event)) {
      sendWorktreePickerCommand("r");
      return true;
    }
    const trayActionConfirmation = trayActionConfirmationFromKey(event);
    if (trayActionConfirmation !== null) {
      sendWorktreePickerCommand(trayActionConfirmation);
      return true;
    }
    const worktreeOverlayNavigation = worktreeOverlayNavigationFromKey(event);
    if (worktreeOverlayNavigation !== null) {
      sendWorktreeOverlayNavigation(worktreeOverlayNavigation);
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
    if (command === "0") {
      const payload = decoder.decode(bytes.slice(1), { stream: true });
      terminal.write(payload);
      return;
    }
    if (command === "1") {
      try {
        applyParentStatus(JSON.parse(statusDecoder.decode(bytes.slice(1))));
      } catch (error) {
        console.warn("Invalid parent status", error);
      }
    }
  });

  socket.addEventListener("close", () => {
    const pendingPayload = decoder.decode();
    if (pendingPayload) {
      terminal.write(pendingPayload);
    }
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
