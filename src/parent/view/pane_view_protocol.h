#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "src/base/terminal_size.h"
#include "src/parent/pane/pane_id.h"

namespace moe::parent {

struct PaneViewOutput {
  std::string tray_key;
  PaneId pane_id;
  std::string bytes;
};

struct PaneViewResize {
  std::string tray_key;
  PaneId pane_id;
  base::TerminalSize size;
};

using PaneViewMessage = std::variant<PaneViewOutput, PaneViewResize>;

[[nodiscard]] std::string encode_pane_view_frame(PaneViewMessage const& message);
[[nodiscard]] std::optional<PaneViewMessage> decode_pane_view_frame(std::string& buffer);

[[nodiscard]] std::string encode_pane_output_payload(PaneViewOutput const& output);
[[nodiscard]] std::optional<PaneViewResize> decode_pane_resize_payload(std::string_view payload);

}  // namespace moe::parent
