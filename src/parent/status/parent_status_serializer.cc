#include "src/parent/status/parent_status_serializer.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

std::string_view pane_mode_name(ParentPaneMode const mode) {
  switch (mode) {
    case ParentPaneMode::NONE:
      return "none";
    case ParentPaneMode::SELECTION:
      return "selection";
    case ParentPaneMode::MOVE_TARGET:
      return "moveTarget";
    case ParentPaneMode::MOVE_DROP:
      return "moveDrop";
    case ParentPaneMode::SWAP_TARGET:
      return "swapTarget";
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

void append_node_id(std::string& output, PaneNodeId const node_id) {
  append_json_string(output, std::to_string(node_id.value()));
}

void append_pane_id(std::string& output, PaneId const pane_id) {
  append_json_string(output, std::to_string(pane_id.value()));
}

void append_node_ids(std::string& output, std::vector<PaneNodeId> const& node_ids) {
  output.push_back('[');
  for (std::size_t index = 0; index < node_ids.size(); ++index) {
    if (index > 0) {
      output.push_back(',');
    }
    append_node_id(output, node_ids[index]);
  }
  output.push_back(']');
}

void append_layout_node(std::string& output, PaneLayout const& layout, PaneNodeId const node_id) {
  PaneLayoutNode const& node = layout.node(node_id);
  output.append(R"({"id":)");
  append_node_id(output, node_id);
  if (node.is_leaf()) {
    output.append(R"(,"pane":)");
    append_pane_id(output, node.pane_id());
    output.push_back('}');
    return;
  }

  PaneSplit const& split = node.split();
  output.append(R"(,"axis":)");
  append_json_string(output,
                     split.axis == PaneSplitAxis::LEFT_TO_RIGHT ? "leftToRight" : "topToBottom");
  output.append(R"(,"percentages":[)");
  for (std::size_t index = 0; index < split.children.size(); ++index) {
    if (index > 0) {
      output.push_back(',');
    }
    output.append(std::to_string(split.children[index].percentage.value()));
  }
  output.append(R"(],"children":[)");
  for (std::size_t index = 0; index < split.children.size(); ++index) {
    if (index > 0) {
      output.push_back(',');
    }
    append_layout_node(output, layout, split.children[index].node_id);
  }
  output.append("]}");
}

void append_pane_view(std::string& output, ParentPaneView const& pane_view) {
  std::optional<PaneLayout> preview;
  if (pane_view.move.has_value()) {
    preview = pane_view.move->preview(pane_view.layout);
  }
  PaneLayout const& rendered_layout = preview.has_value() ? *preview : pane_view.layout;

  output.append(R"({"focusedPane":)");
  append_pane_id(output, pane_view.focused_pane);
  output.append(R"(,"maximized":)");
  output.append(pane_view.maximized ? "true" : "false");
  output.append(R"(,"layout":)");
  append_layout_node(output, rendered_layout, rendered_layout.root_id());
  output.append(R"(,"selection":)");
  if (!pane_view.selection.has_value()) {
    output.append("null");
  } else {
    output.append(R"({"nodes":)");
    append_node_ids(output, pane_view.selection->nodes());
    output.append(R"(,"active":)");
    append_node_id(output, pane_view.selection->active());
    output.push_back('}');
  }
  output.append(R"(,"move":)");
  if (!pane_view.move.has_value()) {
    output.append("null");
  } else {
    output.append(R"({"sourceNodes":)");
    append_node_ids(output, pane_view.move->source().nodes());
    output.append(R"(,"targetNode":)");
    if (pane_view.move->target().has_value()) {
      append_node_id(output, *pane_view.move->target());
    } else {
      output.append("null");
    }
    output.append(R"(,"preview":)");
    output.append(preview.has_value() ? "true" : "false");
    output.push_back('}');
  }
  output.push_back('}');
}

void append_pane_preview(std::string& output, ParentPanePreview const& preview) {
  std::optional<PaneSelection> const no_selection;
  std::optional<PaneMoveSession> const no_move;
  output.append(R"({"trayKey":)");
  append_json_string(output, preview.tray_key);
  output.append(R"(,"origin":{"row":)");
  output.append(std::to_string(preview.origin_row));
  output.append(R"(,"column":)");
  output.append(std::to_string(preview.origin_column));
  output.append(R"(},"size":{"rows":)");
  output.append(std::to_string(preview.size.rows));
  output.append(R"(,"cols":)");
  output.append(std::to_string(preview.size.cols));
  output.append(R"(},"paneView":)");
  append_pane_view(output, ParentPaneView{
                               .layout = preview.layout,
                               .focused_pane = preview.focused_pane,
                               .maximized = preview.maximized,
                               .selection = no_selection,
                               .move = no_move,
                           });
  output.push_back('}');
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
  if (status.worktree_overlay_start_row.has_value()) {
    output.append(R"(,"worktreeOverlayStartRow":)");
    output.append(std::to_string(*status.worktree_overlay_start_row));
  }
  output.append(R"(,"paneMode":)");
  append_json_string(output, pane_mode_name(status.pane_mode));
  output.append(R"(,"paneSelectedNodes":)");
  output.append(std::to_string(status.pane_selected_nodes));
  output.push_back('}');
  return output;
}

std::string serialize_parent_status(ParentStatus const& status, ParentPaneView const& pane_view) {
  std::string output = serialize_parent_status(status);
  output.pop_back();
  output.append(R"(,"paneView":)");
  append_pane_view(output, pane_view);
  output.push_back('}');
  return output;
}

std::string serialize_parent_status(ParentStatus const& status, ParentPaneView const& pane_view,
                                    ParentPanePreview const& pane_preview) {
  std::string output = serialize_parent_status(status, pane_view);
  output.pop_back();
  output.append(R"(,"panePreview":)");
  append_pane_preview(output, pane_preview);
  output.push_back('}');
  return output;
}

}  // namespace moe::parent
