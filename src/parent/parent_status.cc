#include "src/parent/parent_status.h"

#include <array>
#include <string_view>

namespace moe::parent {
namespace {

std::string_view overlay_name(ParentOverlayKind const overlay) {
  switch (overlay) {
    case ParentOverlayKind::NONE:
      return "none";
    case ParentOverlayKind::WORKTREE_MANAGEMENT:
      return "worktreeManagement";
  }
  return "none";
}

void append_json_string(std::string& output, std::string_view const value) {
  constexpr std::array<char, 16> HEX_DIGITS = {'0', '1', '2', '3', '4', '5', '6', '7',
                                               '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

  output.push_back('"');
  for (unsigned char const byte : value) {
    switch (byte) {
      case '"':
        output.append("\\\"");
        break;
      case '\\':
        output.append("\\\\");
        break;
      case '\b':
        output.append("\\b");
        break;
      case '\f':
        output.append("\\f");
        break;
      case '\n':
        output.append("\\n");
        break;
      case '\r':
        output.append("\\r");
        break;
      case '\t':
        output.append("\\t");
        break;
      default:
        if (byte < 0x20U) {
          output.append("\\u00");
          output.push_back(HEX_DIGITS[byte >> 4U]);
          output.push_back(HEX_DIGITS[byte & 0x0FU]);
        } else {
          output.push_back(static_cast<char>(byte));
        }
        break;
    }
  }
  output.push_back('"');
}

}  // namespace

std::string serialize_parent_status(ParentStatus const& status) {
  std::string output = R"({"type":"parent.status","commandMode":)";
  output.append(status.command_mode ? "true" : "false");
  output.append(R"(,"trayKey":)");
  append_json_string(output, status.active_tray.key());
  output.append(R"(,"trayLabel":)");
  append_json_string(output, status.active_tray.label());
  output.append(R"(,"overlay":)");
  append_json_string(output, overlay_name(status.overlay));
  output.push_back('}');
  return output;
}

}  // namespace moe::parent
