#include "src/bridge/browser/browser_assets.h"

namespace moe::bridge {
namespace {

constexpr char const* XTERM_VERSION = "6.0.0";
constexpr char const* XTERM_ADDON_FIT_VERSION = "0.11.0";
constexpr char const* XTERM_ADDON_WEBGL_VERSION = "0.19.0";

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
      <div id="surface">
        <div id="pane-root" aria-label="workspace panes"></div>
        <div id="pane-staging" aria-hidden="true"></div>
        <div id="worktree-overlay-background" aria-hidden="true"></div>
        <div id="terminal" aria-label="workspace terminal"></div>
        <div id="pane-preview-root" aria-label="read-only pane preview"></div>
      </div>
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

}  // namespace moe::bridge
