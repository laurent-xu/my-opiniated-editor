import os
import sys
import unittest

sys.path.append(os.path.dirname(__file__))

from bridge_test_support import fetch_text


def assert_browser_assets(test_case: unittest.TestCase, port: int):
    expected_font_stack = (
        '"JetBrainsMono Nerd Font Mono", "JetBrainsMono NFM", '
        '"JetBrainsMonoNL Nerd Font Mono", "JetBrainsMonoNL NFM", '
        '"MesloLGS NF", "FiraCode Nerd Font Mono", "Hack Nerd Font Mono", '
        '"CaskaydiaCove Nerd Font Mono", "Symbols Nerd Font Mono", '
        '"PowerlineSymbols", "DejaVu Sans Mono for Powerline", '
        '"DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono", '
        '"Cascadia Mono", "JetBrains Mono", monospace'
    )

    html = fetch_text(port, "/")
    test_case.assertIn("@xterm/xterm@6.0.0/css/xterm.css", html)
    test_case.assertIn("@xterm/xterm@6.0.0/lib/xterm.js", html)
    test_case.assertIn("@xterm/addon-fit@0.11.0/lib/addon-fit.js", html)
    test_case.assertIn("@xterm/addon-webgl@0.19.0/lib/addon-webgl.js", html)
    test_case.assertNotIn("@xterm/xterm/css/xterm.css", html)
    test_case.assertNotIn("@xterm/xterm/lib/xterm.js", html)
    test_case.assertNotIn("@xterm/addon-fit/lib/addon-fit.js", html)
    test_case.assertNotIn("@xterm/addon-canvas", html)
    test_case.assertNotIn("@xterm/addon-webgl/lib/addon-webgl.js", html)
    test_case.assertIn("/client.js", html)

    client_js = fetch_text(port, "/client.js")
    test_case.assertTrue(client_js.startswith("(async () => {"), client_js[:32])
    test_case.assertTrue(client_js.rstrip().endswith("})();"))
    expected_browser_to_bridge_discriminators = """\
  const BrowserToBridgeDiscriminator = Object.freeze({
    TERMINAL_INPUT: "0",
    RESIZE: "1",
    SWITCH_ANONYMOUS_TRAY: "2",
    TOGGLE_WORKTREE_OVERLAY: "3",
    TOGGLE_COMMAND_MODE: "5",
    WORKTREE_PICKER_ACTION: "6",
    OVERLAY_NAVIGATION: "7",
  });"""
    expected_bridge_to_browser_discriminators = """\
  const BridgeToBrowserDiscriminator = Object.freeze({
    TERMINAL_OUTPUT: "0",
    PARENT_STATUS: "1",
  });"""
    test_case.assertIn(expected_browser_to_bridge_discriminators, client_js)
    test_case.assertIn(expected_bridge_to_browser_discriminators, client_js)
    expected_discriminator_uses = (
        "sendCommand(BrowserToBridgeDiscriminator.TERMINAL_INPUT, action.data)",
        "sendCommand(BrowserToBridgeDiscriminator.TERMINAL_INPUT, data)",
        "BrowserToBridgeDiscriminator.RESIZE,\n"
        "      JSON.stringify({ columns: terminal.cols, rows: terminal.rows })",
        "BrowserToBridgeDiscriminator.SWITCH_ANONYMOUS_TRAY,\n"
        "      JSON.stringify({ tray: trayNumber })",
        'sendCommand(BrowserToBridgeDiscriminator.TOGGLE_WORKTREE_OVERLAY, "")',
        'sendCommand(BrowserToBridgeDiscriminator.TOGGLE_COMMAND_MODE, "")',
        "sendCommand(BrowserToBridgeDiscriminator.WORKTREE_PICKER_ACTION, command)",
        "sendCommand(BrowserToBridgeDiscriminator.OVERLAY_NAVIGATION, navigation)",
        "if (command === BridgeToBrowserDiscriminator.TERMINAL_OUTPUT)",
        "if (command === BridgeToBrowserDiscriminator.PARENT_STATUS)",
    )
    for discriminator_use in expected_discriminator_uses:
        test_case.assertIn(discriminator_use, client_js)
    test_case.assertNotIn('sendCommand("', client_js)
    test_case.assertNotIn('command === "0"', client_js)
    test_case.assertNotIn('command === "1"', client_js)
    test_case.assertIn("async function awaitTerminalFont()", client_js)
    test_case.assertIn(
        'document.fonts.load(\'14px "JetBrainsMono Nerd Font Mono"\', "\\ue0b0")',
        client_js,
    )
    test_case.assertIn("await document.fonts.ready", client_js)
    test_case.assertIn("await awaitTerminalFont()", client_js)
    test_case.assertIn("new Terminal", client_js)
    test_case.assertIn("customGlyphs: true", client_js)
    test_case.assertIn(f"fontFamily: '{expected_font_stack}'", client_js)
    test_case.assertIn("lineHeight: 1.15", client_js)
    test_case.assertIn("new WebglAddon.WebglAddon()", client_js)
    test_case.assertIn("terminal.loadAddon(webglAddon)", client_js)
    test_case.assertIn('console.warn("WebGL terminal renderer unavailable"', client_js)
    test_case.assertIn(
        "new WebSocket(`${protocol}//${window.location.host}${websocketPath}`",
        client_js,
    )
    test_case.assertIn("new URLSearchParams(window.location.search)", client_js)
    test_case.assertIn(
        "const payload = decoder.decode(bytes.slice(1), { stream: true })",
        client_js,
    )
    test_case.assertIn(
        "applyParentStatus(JSON.parse(statusDecoder.decode(bytes.slice(1))))",
        client_js,
    )
    test_case.assertIn("const pendingPayload = decoder.decode()", client_js)
    test_case.assertIn("terminal.attachCustomKeyEventHandler", client_js)
    test_case.assertIn('document.addEventListener("keydown"', client_js)
    test_case.assertIn('let activeTrayLabel = "tray 1"', client_js)
    test_case.assertIn("let commandMode = false", client_js)
    test_case.assertNotIn("let worktreeManagerOpen", client_js)
    test_case.assertIn('parts.push("command")', client_js)
    test_case.assertNotIn("activeTrayNumber", client_js)
    test_case.assertNotIn("setCommandMode", client_js)
    test_case.assertIn("activeTrayLabel = status.trayLabel", client_js)
    test_case.assertIn('event.key === "Escape"', client_js)
    test_case.assertIn('event.key === "Shift"', client_js)
    test_case.assertIn('TERMINAL: "terminal"', client_js)
    test_case.assertIn('COMMAND: "command"', client_js)
    test_case.assertIn('PASS_TO_XTERM: "passToXterm"', client_js)
    test_case.assertIn("const commandModeActionClassifiers = [", client_js)
    test_case.assertIn("classifyModifierOnlyAction", client_js)
    test_case.assertIn("classifyTraySwitchAction", client_js)
    test_case.assertIn("classifyShiftedCommandAction", client_js)
    test_case.assertIn("classifyTrayConfirmationAction", client_js)
    test_case.assertIn("classifyOverlayNavigationAction", client_js)
    test_case.assertIn("function classifyTerminalKey(event, state)", client_js)
    test_case.assertIn(
        "const state = commandMode ? KeyboardState.COMMAND : KeyboardState.TERMINAL",
        client_js,
    )
    test_case.assertIn(
        "return dispatchKeyAction(event, classifyTerminalKey(event, state))", client_js
    )
    test_case.assertIn('/^Digit([1-9])$/.exec(event.code || "")', client_js)
    test_case.assertNotIn('event.code === "KeyT" || event.key === "T"', client_js)
    test_case.assertIn('code: "KeyW"', client_js)
    test_case.assertIn('code: "KeyC"', client_js)
    test_case.assertIn('code: "KeyR"', client_js)
    test_case.assertIn("toggleWorktreeManager()", client_js)
    test_case.assertIn('command: "c"', client_js)
    test_case.assertIn('command: "r"', client_js)
    test_case.assertIn('ArrowUp: "up"', client_js)
    test_case.assertIn('ArrowDown: "down"', client_js)
    test_case.assertIn('return "enter"', client_js)
    test_case.assertIn('return event.shiftKey ? "backtab" : "tab"', client_js)
    test_case.assertNotIn("toggleWorktreePicker()", client_js)
    test_case.assertNotIn("tray find not implemented", client_js)
    test_case.assertIn('event.key === "Tab"', client_js)
    test_case.assertIn('event.code === "Tab"', client_js)
    test_case.assertIn("event.keyCode === 9", client_js)
    test_case.assertIn("terminalShouldReceiveKey()", client_js)
    test_case.assertIn(
        "if (terminalShouldReceiveKey() && handleTerminalKey(event))", client_js
    )
    test_case.assertIn("event.stopImmediatePropagation()", client_js)
    test_case.assertIn(
        'if (event.type === "keydown" && handleTerminalKey(event))', client_js
    )
    test_case.assertIn('data: event.shiftKey ? "\\x1b[Z" : "\\t"', client_js)

    css = fetch_text(port, "/style.css")
    test_case.assertIn("#terminal", css)
    test_case.assertNotIn('@font-face {\n  font-family: "Moe Terminal Nerd Font"', css)
    test_case.assertIn(f"font-family: {expected_font_stack};", css)
