#include "src/bridge/browser/browser_assets.h"
#include "src/bridge/browser/browser_font_families.h"
#include "src/bridge/protocol/application_message_discriminators.h"

namespace moe::bridge {

std::string browser_client_js() {
  return std::string(R"JS((async () => {
  const terminalElement = document.getElementById("terminal");
  const statusElement = document.getElementById("status");

  async function awaitTerminalFont() {
    if (!document.fonts || !document.fonts.load) {
      return;
    }
    try {
      await document.fonts.load('14px )JS") +
         browser::PRIMARY_TERMINAL_FONT_FAMILY + R"JS(', "\ue0b0");
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
         browser::TERMINAL_FONT_FAMILY + R"JS(',
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
  const socket = new WebSocket(`${protocol}//${window.location.host}/ws`, "workspace-pty");
  socket.binaryType = "arraybuffer";
  let connectionState = "connecting";
  let activeTrayLabel = "tray 1";
  let commandMode = false;
  let activeOverlay = "none";

  const KeyboardState = Object.freeze({
    TERMINAL: "terminal",
    COMMAND: "command",
  });
  const KeyActionType = Object.freeze({
    PASS_TO_XTERM: "passToXterm",
    CONSUME: "consume",
    REFRESH_STATUS: "refreshStatus",
    TOGGLE_COMMAND_MODE: "toggleCommandMode",
    SWITCH_TRAY: "switchTray",
    TOGGLE_WORKTREE_MANAGER: "toggleWorktreeManager",
    WORKTREE_PICKER_COMMAND: "worktreePickerCommand",
    WORKTREE_OVERLAY_NAVIGATION: "worktreeOverlayNavigation",
    PANE_ACTION: "paneAction",
    TERMINAL_INPUT: "terminalInput",
  });
  const BrowserToBridgeDiscriminator = Object.freeze({
    TERMINAL_INPUT: ")JS" +
         protocol::browser_to_bridge_discriminator::TERMINAL_INPUT + R"JS(",
    RESIZE: ")JS" +
         protocol::browser_to_bridge_discriminator::RESIZE + R"JS(",
    SWITCH_ANONYMOUS_TRAY: ")JS" +
         protocol::browser_to_bridge_discriminator::SWITCH_ANONYMOUS_TRAY + R"JS(",
    TOGGLE_WORKTREE_OVERLAY: ")JS" +
         protocol::browser_to_bridge_discriminator::TOGGLE_WORKTREE_OVERLAY + R"JS(",
    TOGGLE_COMMAND_MODE: ")JS" +
         protocol::browser_to_bridge_discriminator::TOGGLE_COMMAND_MODE + R"JS(",
    WORKTREE_PICKER_ACTION: ")JS" +
         protocol::browser_to_bridge_discriminator::WORKTREE_PICKER_ACTION + R"JS(",
    OVERLAY_NAVIGATION: ")JS" +
         protocol::browser_to_bridge_discriminator::OVERLAY_NAVIGATION + R"JS(",
    PANE_ACTION: ")JS" +
         protocol::browser_to_bridge_discriminator::PANE_ACTION + R"JS(",
  });
  const BridgeToBrowserDiscriminator = Object.freeze({
    TERMINAL_OUTPUT: ")JS" +
         protocol::bridge_to_browser_discriminator::TERMINAL_OUTPUT + R"JS(",
    PARENT_STATUS: ")JS" +
         protocol::bridge_to_browser_discriminator::PARENT_STATUS + R"JS(",
  });

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
    if (typeof status.overlay === "string") {
      activeOverlay = status.overlay;
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
    sendCommand(
      BrowserToBridgeDiscriminator.SWITCH_ANONYMOUS_TRAY,
      JSON.stringify({ tray: trayNumber }),
    );
  }

  function toggleWorktreeManager() {
    sendCommand(BrowserToBridgeDiscriminator.TOGGLE_WORKTREE_OVERLAY, "");
  }

  function sendWorktreePickerCommand(command) {
    sendCommand(BrowserToBridgeDiscriminator.WORKTREE_PICKER_ACTION, command);
  }

  function sendWorktreeOverlayNavigation(navigation) {
    sendCommand(BrowserToBridgeDiscriminator.OVERLAY_NAVIGATION, navigation);
  }

  function sendPaneAction(action) {
    sendCommand(BrowserToBridgeDiscriminator.PANE_ACTION, action);
  }

  function fitAndSendSize() {
    fitAddon.fit();
    sendCommand(
      BrowserToBridgeDiscriminator.RESIZE,
      JSON.stringify({ columns: terminal.cols, rows: terminal.rows }),
    );
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

  function isShiftEscapeKey(event) {
    return isEscapeKey(event) && event.shiftKey && !event.altKey && !event.ctrlKey && !event.metaKey;
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

  const globalShiftedCommandBindings = [
    {
      code: "KeyW",
      key: "W",
      action: { type: KeyActionType.TOGGLE_WORKTREE_MANAGER },
    },
  ];
  const worktreeShiftedCommandBindings = [
    {
      code: "KeyC",
      key: "C",
      action: { type: KeyActionType.WORKTREE_PICKER_COMMAND, command: "c" },
    },
    {
      code: "KeyR",
      key: "R",
      action: { type: KeyActionType.WORKTREE_PICKER_COMMAND, command: "r" },
    },
  ];
  const paneShiftedCommandBindings = [
    { code: "ArrowUp", action: { type: KeyActionType.PANE_ACTION, action: "up" } },
    { code: "ArrowDown", action: { type: KeyActionType.PANE_ACTION, action: "down" } },
    { code: "ArrowLeft", action: { type: KeyActionType.PANE_ACTION, action: "left" } },
    { code: "ArrowRight", action: { type: KeyActionType.PANE_ACTION, action: "right" } },
    { code: "KeyV", action: { type: KeyActionType.PANE_ACTION, action: "splitLeftToRight" } },
    { code: "KeyH", action: { type: KeyActionType.PANE_ACTION, action: "splitAboveBelow" } },
    { code: "KeyS", action: { type: KeyActionType.PANE_ACTION, action: "toggleSelectionOrSwap" } },
    { code: "BracketLeft", action: { type: KeyActionType.PANE_ACTION, action: "promote" } },
    { code: "BracketRight", action: { type: KeyActionType.PANE_ACTION, action: "descend" } },
    { code: "Equal", key: "+", action: { type: KeyActionType.PANE_ACTION, action: "grow" } },
    { code: "Minus", action: { type: KeyActionType.PANE_ACTION, action: "shrink" } },
    { code: "KeyE", action: { type: KeyActionType.PANE_ACTION, action: "equalize" } },
    { code: "KeyM", action: { type: KeyActionType.PANE_ACTION, action: "toggleMove" } },
    { code: "Enter", action: { type: KeyActionType.PANE_ACTION, action: "confirmMove" } },
    { code: "KeyR", action: { type: KeyActionType.PANE_ACTION, action: "rotate" } },
    { code: "KeyZ", action: { type: KeyActionType.PANE_ACTION, action: "toggleMaximize" } },
    { code: "KeyX", action: { type: KeyActionType.PANE_ACTION, action: "close" } },
  ];

  function classifyModifierOnlyAction(event) {
    return isModifierOnlyKey(event) ? { type: KeyActionType.CONSUME } : null;
  }

  function classifyTraySwitchAction(event) {
    const trayNumber = trayNumberFromShiftDigit(event);
    return trayNumber === null ? null : { type: KeyActionType.SWITCH_TRAY, trayNumber };
  }

  function classifyShiftedCommandAction(event) {
    if (!event.shiftKey || event.altKey || event.ctrlKey || event.metaKey) {
      return null;
    }
    const contextualBindings = activeOverlay === "worktreeManagement"
      ? worktreeShiftedCommandBindings
      : paneShiftedCommandBindings;
    const binding = [...globalShiftedCommandBindings, ...contextualBindings].find(
      (candidate) => event.code === candidate.code ||
        (candidate.key !== undefined && event.key === candidate.key),
    );
    return binding ? binding.action : null;
  }

  function classifyTrayConfirmationAction(event) {
    const command = trayActionConfirmationFromKey(event);
    return command === null ? null : { type: KeyActionType.WORKTREE_PICKER_COMMAND, command };
  }

  function classifyOverlayNavigationAction(event) {
    const navigation = worktreeOverlayNavigationFromKey(event);
    return navigation === null ? null : {
      type: KeyActionType.WORKTREE_OVERLAY_NAVIGATION,
      navigation,
    };
  }

  const commandModeActionClassifiers = [
    classifyModifierOnlyAction,
    classifyTraySwitchAction,
    classifyShiftedCommandAction,
    classifyTrayConfirmationAction,
    classifyOverlayNavigationAction,
  ];

  function classifyCommandModeAction(event) {
    for (const classifyAction of commandModeActionClassifiers) {
      const action = classifyAction(event);
      if (action !== null) {
        return action;
      }
    }
    return { type: KeyActionType.REFRESH_STATUS };
  }

  function classifyTerminalKey(event, state) {
    if (isShiftEscapeKey(event) && state === KeyboardState.TERMINAL) {
      return { type: KeyActionType.TERMINAL_INPUT, data: "\x1b" };
    }
    if (isEscapeKey(event)) {
      return { type: KeyActionType.TOGGLE_COMMAND_MODE };
    }
    if (state === KeyboardState.COMMAND) {
      return classifyCommandModeAction(event);
    }
    if (isTabKey(event) && !event.altKey && !event.ctrlKey && !event.metaKey) {
      return {
        type: KeyActionType.TERMINAL_INPUT,
        data: event.shiftKey ? "\x1b[Z" : "\t",
      };
    }
    return { type: KeyActionType.PASS_TO_XTERM };
  }

  function dispatchKeyAction(event, action) {
    if (action.type === KeyActionType.PASS_TO_XTERM) {
      return false;
    }

    event.preventDefault();
    if (action.type === KeyActionType.TOGGLE_COMMAND_MODE) {
      sendCommand(BrowserToBridgeDiscriminator.TOGGLE_COMMAND_MODE, "");
    } else if (action.type === KeyActionType.SWITCH_TRAY) {
      sendTraySwitch(action.trayNumber);
    } else if (action.type === KeyActionType.TOGGLE_WORKTREE_MANAGER) {
      toggleWorktreeManager();
    } else if (action.type === KeyActionType.WORKTREE_PICKER_COMMAND) {
      sendWorktreePickerCommand(action.command);
    } else if (action.type === KeyActionType.WORKTREE_OVERLAY_NAVIGATION) {
      sendWorktreeOverlayNavigation(action.navigation);
    } else if (action.type === KeyActionType.PANE_ACTION) {
      sendPaneAction(action.action);
    } else if (action.type === KeyActionType.TERMINAL_INPUT) {
      sendCommand(BrowserToBridgeDiscriminator.TERMINAL_INPUT, action.data);
    } else if (action.type === KeyActionType.REFRESH_STATUS) {
      renderStatus();
    }
    return true;
  }

  function handleTerminalKey(event) {
    const state = commandMode ? KeyboardState.COMMAND : KeyboardState.TERMINAL;
    return dispatchKeyAction(event, classifyTerminalKey(event, state));
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
    sendCommand(BrowserToBridgeDiscriminator.TERMINAL_INPUT, data);
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
    if (command === BridgeToBrowserDiscriminator.TERMINAL_OUTPUT) {
      const payload = decoder.decode(bytes.slice(1), { stream: true });
      terminal.write(payload);
      return;
    }
    if (command === BridgeToBrowserDiscriminator.PARENT_STATUS) {
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
