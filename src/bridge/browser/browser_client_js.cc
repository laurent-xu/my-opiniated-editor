#include "src/bridge/browser/browser_assets.h"
#include "src/bridge/browser/browser_font_families.h"
#include "src/bridge/protocol/application_message_discriminators.h"

namespace moe::bridge {

std::string browser_client_js() {
  return std::string(R"JS((async () => {
  const terminalElement = document.getElementById("terminal");
  const paneRootElement = document.getElementById("pane-root");
  const panePreviewRootElement = document.getElementById("pane-preview-root");
  const paneStagingElement = document.getElementById("pane-staging");
  const worktreeOverlayBackgroundElement =
    document.getElementById("worktree-overlay-background");
  const surfaceElement = document.getElementById("surface");
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

  function terminalOptions(transparent = false) {
    return {
      allowTransparency: transparent,
      cursorBlink: true,
      convertEol: true,
      customGlyphs: true,
      fontFamily: ')JS" +
         browser::TERMINAL_FONT_FAMILY + R"JS(',
      fontSize: 14,
      lineHeight: 1.15,
      theme: {
        background: transparent ? "rgba(0, 0, 0, 0)" : "#0b0d0e",
        foreground: "#d9e2df",
        cursor: "#f5d06f"
      }
    };
  }

  const terminal = new Terminal(terminalOptions(true));
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
  const paneIdentityDecoder = new TextDecoder();
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const socket = new WebSocket(`${protocol}//${window.location.host}/ws`, "workspace-pty");
  socket.binaryType = "arraybuffer";
  let connectionState = "connecting";
  let activeTrayKey = "anonymous:1";
  let activeTrayLabel = "tray 1";
  let commandMode = false;
  let activeOverlay = "none";
  let paneMode = "none";
  let paneSelectedNodes = 0;
  let activePaneView = null;
  let activePanePreview = null;
  let worktreeOverlayStartRow = null;
  const paneTerminals = new Map();

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
    PANE_RESIZE: ")JS" +
         protocol::browser_to_bridge_discriminator::PANE_RESIZE + R"JS(",
  });
  const BridgeToBrowserDiscriminator = Object.freeze({
    TERMINAL_OUTPUT: ")JS" +
         protocol::bridge_to_browser_discriminator::TERMINAL_OUTPUT + R"JS(",
    PARENT_STATUS: ")JS" +
         protocol::bridge_to_browser_discriminator::PARENT_STATUS + R"JS(",
    PANE_OUTPUT: ")JS" +
         protocol::bridge_to_browser_discriminator::PANE_OUTPUT + R"JS(",
  });

  function renderStatus() {
    const parts = [connectionState, activeTrayLabel];
    if (activeOverlay === "worktreeManagement") {
      parts.push("worktrees");
    } else if (paneMode === "selection") {
      parts.push(`selection ${paneSelectedNodes}`);
    } else if (paneMode === "moveTarget") {
      parts.push(`move ${paneSelectedNodes}: choose target`);
    } else if (paneMode === "moveDrop") {
      parts.push(`move ${paneSelectedNodes}: choose side`);
    } else if (paneMode === "swapTarget") {
      parts.push("swap: choose target");
    }
    if (commandMode) {
      parts.push("command");
      if (activeOverlay === "worktreeManagement") {
        parts.push("Arrows navigate; Shift+C clear; Shift+R remove");
      } else if (paneMode === "selection") {
        parts.push("Shift+Arrows range; Shift+[ ] level; Shift++/- size; Shift+M move");
      } else if (paneMode === "moveTarget") {
        parts.push("Shift+Arrows target; Shift+[ ] level; Shift+Enter lock; Shift+M cancel");
      } else if (paneMode === "moveDrop") {
        parts.push("Shift+Arrows side; Shift+Enter confirm; Shift+M cancel");
      } else if (paneMode === "swapTarget") {
        parts.push("Shift+Arrows target; Shift+Enter confirm; Shift+S move; Shift+M cancel");
      } else {
        parts.push("Shift+V/H split; Shift+S select; Shift+M move");
      }
    }
    statusElement.textContent = parts.join(" | ");
  }

  function setConnectionState(text) {
    connectionState = text;
    renderStatus();
  }

  function paneTerminalKey(trayKey, paneId) {
    return `${trayKey}\u0000${paneId}`;
  }

  function paneRecord(trayKey, paneId) {
    const key = paneTerminalKey(trayKey, paneId);
    let record = paneTerminals.get(key);
    if (record !== undefined) {
      return record;
    }
    const element = document.createElement("div");
    element.className = "pane-terminal";
    record = {
      key,
      trayKey,
      paneId,
      element,
      terminal: null,
      fitAddon: null,
      lastRows: 0,
      lastCols: 0,
    };
    paneTerminals.set(key, record);
    return record;
  }

  function mountPaneTerminal(record) {
    if (record.terminal !== null) {
      return;
    }
    const paneTerminal = new Terminal(terminalOptions());
    const paneFitAddon = new FitAddon.FitAddon();
    paneTerminal.loadAddon(paneFitAddon);
    paneTerminal.open(record.element);
    paneTerminal.attachCustomKeyEventHandler((event) => {
      if (event.type === "keydown" && handleTerminalKey(event)) {
        return false;
      }
      return true;
    });
    paneTerminal.onData((data) => {
      sendCommand(BrowserToBridgeDiscriminator.TERMINAL_INPUT, data);
    });
    record.terminal = paneTerminal;
    record.fitAddon = paneFitAddon;
  }

  function appendPaneIdentity(view, offset, trayKey, paneId) {
    const trayBytes = encoder.encode(trayKey);
    if (trayBytes.length > 0xffff) {
      return null;
    }
    view.setUint16(offset, trayBytes.length);
    offset += 2;
    new Uint8Array(view.buffer, offset, trayBytes.length).set(trayBytes);
    offset += trayBytes.length;
    view.setBigUint64(offset, BigInt(paneId));
    return offset + 8;
  }

  function sendPaneResize(record) {
    if (socket.readyState !== WebSocket.OPEN || record.terminal === null) {
      return;
    }
    const trayBytes = encoder.encode(record.trayKey);
    const body = new Uint8Array(1 + 2 + trayBytes.length + 8 + 4 + 4);
    body[0] = BrowserToBridgeDiscriminator.PANE_RESIZE.charCodeAt(0);
    const view = new DataView(body.buffer);
    const sizeOffset = appendPaneIdentity(view, 1, record.trayKey, record.paneId);
    if (sizeOffset === null) {
      return;
    }
    view.setUint32(sizeOffset, record.terminal.rows);
    view.setUint32(sizeOffset + 4, record.terminal.cols);
    socket.send(body);
  }

  function fitPaneTerminalsIn(rootElement) {
    for (const record of paneTerminals.values()) {
      if (record.terminal === null || !rootElement.contains(record.element) ||
          record.element.clientWidth < 4 || record.element.clientHeight < 4) {
        continue;
      }
      record.fitAddon.fit();
      if (record.lastRows !== record.terminal.rows || record.lastCols !== record.terminal.cols) {
        record.lastRows = record.terminal.rows;
        record.lastCols = record.terminal.cols;
        sendPaneResize(record);
      }
    }
  }

  function fitPreviewPaneTerminalsIn(rootElement) {
    for (const record of paneTerminals.values()) {
      if (record.terminal === null || !rootElement.contains(record.element) ||
          record.element.clientWidth < 4 || record.element.clientHeight < 4) {
        continue;
      }
      const dimensions = record.fitAddon.proposeDimensions();
      if (dimensions === undefined || dimensions.rows <= 0) {
        continue;
      }
      const preservedCols = record.lastCols > 0 ? record.lastCols : record.terminal.cols;
      if (record.terminal.rows !== dimensions.rows || record.terminal.cols !== preservedCols) {
        record.terminal.resize(preservedCols, dimensions.rows);
      }
    }
  }

  function fitVisiblePaneTerminals() {
    if (surfaceElement.classList.contains("pane-view-active") ||
        surfaceElement.classList.contains("pane-overlay-background-active")) {
      fitPaneTerminalsIn(paneRootElement);
    }
    if (surfaceElement.classList.contains("pane-preview-active")) {
      fitPreviewPaneTerminalsIn(panePreviewRootElement);
    }
  }

  function collectPaneIds(node, output) {
    if (typeof node.pane === "string") {
      output.add(node.pane);
      return;
    }
    for (const child of node.children || []) {
      collectPaneIds(child, output);
    }
  }

  function findPaneNode(node, paneId) {
    if (node.pane === paneId) {
      return node;
    }
    for (const child of node.children || []) {
      const found = findPaneNode(child, paneId);
      if (found !== null) {
        return found;
      }
    }
    return null;
  }

  function findPaneNodeById(node, nodeId) {
    if (node.id === nodeId) {
      return node;
    }
    for (const child of node.children || []) {
      const found = findPaneNodeById(child, nodeId);
      if (found !== null) {
        return found;
      }
    }
    return null;
  }

  function findParentNodeId(node, nodeId, parentId = null) {
    if (node.id === nodeId) {
      return parentId;
    }
    for (const child of node.children || []) {
      const found = findParentNodeId(child, nodeId, node.id);
      if (found !== null) {
        return found;
      }
    }
    return null;
  }

  function paneDecorations(paneView) {
    const selectedPaneIds = new Set();
    const selection = paneView.selection;
    if (selection !== null) {
      for (const nodeId of selection.nodes) {
        const selectedNode = findPaneNodeById(paneView.layout, nodeId);
        if (selectedNode !== null) {
          collectPaneIds(selectedNode, selectedPaneIds);
        }
      }
    }
    return {
      selectedPaneIds,
      selectionParentId: selection === null
        ? null
        : findParentNodeId(paneView.layout, selection.active),
    };
  }

  function buildPaneNode(node, paneView, trayKey, decorations, recordsToMount) {
    const element = document.createElement("div");
    element.classList.add("pane-node");
    element.dataset.nodeId = node.id;
    const selected = paneView.selection;
    if (selected !== null && selected.nodes.includes(node.id)) {
      element.classList.add("pane-selected");
      if (selected.active === node.id) {
        element.classList.add("pane-selection-active");
      }
    }
    const move = paneView.move;
    if (move !== null && move.sourceNodes.includes(node.id)) {
      element.classList.add(move.preview ? "pane-move-preview" : "pane-move-source");
    }
    if (move !== null && move.targetNode === node.id) {
      element.classList.add("pane-move-target");
    }

    if (typeof node.pane === "string") {
      element.classList.add("pane-leaf");
      if (node.pane === paneView.focusedPane) {
        element.classList.add("pane-focused");
      }
      if ((selected !== null && !decorations.selectedPaneIds.has(node.pane)) ||
          (selected === null && move === null && node.pane !== paneView.focusedPane)) {
        element.classList.add("pane-muted");
      }
      const record = paneRecord(trayKey, node.pane);
      element.append(record.element);
      recordsToMount.push(record);
      return element;
    }

    element.classList.add("pane-split");
    element.classList.add(node.axis === "leftToRight"
      ? "pane-split-left-to-right"
      : "pane-split-top-to-bottom");
    if (selected !== null) {
      element.classList.add("pane-hierarchy-group");
      if (node.id === decorations.selectionParentId) {
        element.classList.add("pane-hierarchy-active");
      }
    }
    for (let index = 0; index < node.children.length; ++index) {
      const child = buildPaneNode(
        node.children[index], paneView, trayKey, decorations, recordsToMount);
      child.style.flex = `0 0 ${node.percentages[index]}%`;
      element.append(child);
    }
    return element;
  }

  function stagePaneTerminalsIn(rootElement) {
    for (const record of paneTerminals.values()) {
      if (rootElement.contains(record.element)) {
        paneStagingElement.append(record.element);
      }
    }
  }

  function renderPaneView(paneView) {
    activePaneView = paneView;
    const panesVisible = paneView !== null && activeOverlay === "none";
    const overlayBackgroundVisible =
      paneView !== null && activeOverlay === "worktreeManagement";
    surfaceElement.classList.toggle("pane-view-active", panesVisible);
    surfaceElement.classList.toggle(
      "pane-overlay-background-active", overlayBackgroundVisible);
    if (paneView === null || paneView.layout === undefined) {
      paneRootElement.replaceChildren();
      return;
    }

    const currentPaneIds = new Set();
    collectPaneIds(paneView.layout, currentPaneIds);
    for (const [key, record] of paneTerminals) {
      if (record.trayKey === activeTrayKey && !currentPaneIds.has(record.paneId)) {
        if (record.terminal !== null) {
          record.terminal.dispose();
        }
        record.element.remove();
        paneTerminals.delete(key);
      }
    }

    stagePaneTerminalsIn(paneRootElement);
    const renderedRoot = paneView.maximized
      ? findPaneNode(paneView.layout, paneView.focusedPane)
      : paneView.layout;
    if (renderedRoot === null) {
      return;
    }
    const recordsToMount = [];
    paneRootElement.replaceChildren(buildPaneNode(
      renderedRoot, paneView, activeTrayKey, paneDecorations(paneView), recordsToMount));
    for (const record of recordsToMount) {
      mountPaneTerminal(record);
    }
    requestAnimationFrame(() => {
      fitVisiblePaneTerminals();
      if (panesVisible) {
        const focused = paneTerminals.get(paneTerminalKey(activeTrayKey, paneView.focusedPane));
        if (focused !== undefined && focused.terminal !== null) {
          focused.terminal.focus();
        }
      }
    });
  }

  function positionPanePreview(preview) {
    const screen = terminalElement.querySelector(".xterm-screen");
    if (screen === null || terminal.rows <= 0 || terminal.cols <= 0) {
      return;
    }
    const terminalRect = terminalElement.getBoundingClientRect();
    const screenRect = screen.getBoundingClientRect();
    const cellWidth = screenRect.width / terminal.cols;
    const cellHeight = screenRect.height / terminal.rows;
    panePreviewRootElement.style.left =
      `${screenRect.left - terminalRect.left + preview.origin.column * cellWidth}px`;
    panePreviewRootElement.style.top =
      `${screenRect.top - terminalRect.top + preview.origin.row * cellHeight}px`;
    panePreviewRootElement.style.width = `${preview.size.cols * cellWidth}px`;
    panePreviewRootElement.style.height = `${preview.size.rows * cellHeight}px`;
  }

  function renderWorktreeOverlayBackground() {
    const visible = activeOverlay === "worktreeManagement" &&
      Number.isInteger(worktreeOverlayStartRow);
    surfaceElement.classList.toggle("worktree-overlay-background-active", visible);
    if (!visible) {
      return;
    }
    const screen = terminalElement.querySelector(".xterm-screen");
    if (screen === null || terminal.rows <= 0 || terminal.cols <= 0) {
      return;
    }
    const terminalRect = terminalElement.getBoundingClientRect();
    const screenRect = screen.getBoundingClientRect();
    const cellHeight = screenRect.height / terminal.rows;
    const startRow = Math.min(Math.max(worktreeOverlayStartRow, 0), terminal.rows - 1);
    worktreeOverlayBackgroundElement.style.left = `${screenRect.left - terminalRect.left}px`;
    worktreeOverlayBackgroundElement.style.top =
      `${screenRect.top - terminalRect.top + startRow * cellHeight}px`;
    worktreeOverlayBackgroundElement.style.width = `${screenRect.width}px`;
    worktreeOverlayBackgroundElement.style.height = `${(terminal.rows - startRow) * cellHeight}px`;
  }

  function renderPanePreview(preview) {
    activePanePreview = preview;
    stagePaneTerminalsIn(panePreviewRootElement);
    const previewVisible = preview !== null && activeOverlay === "worktreeManagement" &&
      preview.paneView !== undefined && preview.paneView.layout !== undefined;
    surfaceElement.classList.toggle("pane-preview-active", previewVisible);
    if (!previewVisible) {
      panePreviewRootElement.replaceChildren();
      requestAnimationFrame(() => {
        if (activeOverlay !== "none") {
          terminal.focus();
        }
      });
      return;
    }

    const paneView = preview.paneView;
    const renderedRoot = paneView.maximized
      ? findPaneNode(paneView.layout, paneView.focusedPane)
      : paneView.layout;
    if (renderedRoot === null) {
      return;
    }
    const recordsToMount = [];
    panePreviewRootElement.replaceChildren(buildPaneNode(
      renderedRoot, paneView, preview.trayKey, paneDecorations(paneView), recordsToMount));
    for (const record of recordsToMount) {
      mountPaneTerminal(record);
    }
    positionPanePreview(preview);
    requestAnimationFrame(() => {
      positionPanePreview(preview);
      fitVisiblePaneTerminals();
      terminal.focus();
    });
  }

  function consumePaneOutput(payload) {
    if (payload.length < 10) {
      return;
    }
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    const trayLength = view.getUint16(0);
    const paneOffset = 2 + trayLength;
    if (payload.length < paneOffset + 8) {
      return;
    }
    const trayKey = paneIdentityDecoder.decode(payload.slice(2, paneOffset));
    const paneId = view.getBigUint64(paneOffset).toString();
    const bytes = payload.slice(paneOffset + 8);
    const record = paneRecord(trayKey, paneId);
    if (record.terminal === null) {
      paneStagingElement.append(record.element);
      mountPaneTerminal(record);
    }
    record.terminal.write(bytes);
  }

  function applyParentStatus(status) {
    if (status.type !== "parent.status") {
      return;
    }
    commandMode = status.commandMode === true;
    if (typeof status.trayKey === "string" && status.trayKey.length > 0) {
      activeTrayKey = status.trayKey;
    }
    if (typeof status.trayLabel === "string" && status.trayLabel.length > 0) {
      activeTrayLabel = status.trayLabel;
    }
    if (typeof status.overlay === "string") {
      activeOverlay = status.overlay;
    }
    worktreeOverlayStartRow = Number.isInteger(status.worktreeOverlayStartRow)
      ? status.worktreeOverlayStartRow
      : null;
    if (typeof status.paneMode === "string") {
      paneMode = status.paneMode;
    }
    if (Number.isInteger(status.paneSelectedNodes) && status.paneSelectedNodes >= 0) {
      paneSelectedNodes = status.paneSelectedNodes;
    }
    renderPaneView(status.paneView || null);
    renderPanePreview(status.panePreview || null);
    renderWorktreeOverlayBackground();
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
    requestAnimationFrame(fitVisiblePaneTerminals);
    requestAnimationFrame(renderWorktreeOverlayBackground);
    if (activePanePreview !== null) {
      requestAnimationFrame(() => positionPanePreview(activePanePreview));
    }
  }

  function terminalShouldReceiveKey() {
    const activeElement = document.activeElement;
    return activeElement === document.body || activeElement === surfaceElement ||
      surfaceElement.contains(activeElement);
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
      return;
    }
    if (command === BridgeToBrowserDiscriminator.PANE_OUTPUT) {
      consumePaneOutput(bytes.slice(1));
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
