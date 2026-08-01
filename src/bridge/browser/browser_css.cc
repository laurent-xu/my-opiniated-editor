#include "src/bridge/browser/browser_assets.h"
#include "src/bridge/browser/browser_font_families.h"

namespace moe::bridge {

std::string browser_css() {
  return std::string(R"CSS(:root {
  color-scheme: dark;
  font-family: )CSS") +
         browser::TERMINAL_FONT_FAMILY + R"CSS(;
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

}  // namespace moe::bridge
