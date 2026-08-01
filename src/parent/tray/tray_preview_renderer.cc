#include "src/parent/tray/tray_preview_renderer.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include "src/parent/terminal/terminal_screen.h"
#include "src/parent/tray/tray.h"
#include "src/parent/tray/tray_id_kind.h"

namespace moe::parent {
namespace {

std::string preview_target(TrayId const& id) {
  if (id.kind() == TrayIdKind::ANONYMOUS) {
    return "/anonymous/" + std::to_string(id.anonymous_number().value());
  }
  return id.worktree_root().string();
}

std::string render_preview_header(TrayPreviewRequest const& preview) {
  std::size_t const width = static_cast<std::size_t>(std::max(preview.size.cols, 0));
  if (width == 0U || preview.size.rows <= 0) {
    return {};
  }

  constexpr std::string_view PREFIX = "Preview: ";
  std::string const target = preview_target(preview.tray_id);
  std::string title(PREFIX);
  if (title.size() + target.size() <= width) {
    title += target;
  } else if (width > title.size() + 3U) {
    std::size_t const target_width = width - title.size() - 3U;
    title += "..." + target.substr(target.size() - target_width);
  }
  title.resize(width, ' ');

  return "\x1b[?25l\x1b[" + std::to_string(preview.origin.row + 1) + ";" +
         std::to_string(preview.origin.column + 1) + "H\x1b[48;5;236m\x1b[38;5;252m" + title +
         "\x1b[0m";
}

TrayPreviewRequest content_preview_for(TrayPreviewRequest const& preview) {
  return TrayPreviewRequest{
      .tray_id = preview.tray_id,
      .origin =
          TerminalPosition{
              .row = preview.origin.row + 1,
              .column = preview.origin.column,
          },
      .size =
          base::TerminalSize{
              .rows = std::max(preview.size.rows - 1, 0),
              .cols = preview.size.cols,
          },
  };
}

}  // namespace

std::string render_tray_preview(TrayPreviewRequest const& preview,
                                Tray const* const previewed_tray) {
  TrayPreviewRequest const content_preview = content_preview_for(preview);
  std::string output =
      previewed_tray == nullptr
          ? TerminalScreen::render_blank_region_snapshot(content_preview.origin,
                                                         content_preview.size)
          : previewed_tray->preview_output(content_preview.origin, content_preview.size);
  output += render_preview_header(preview);
  return output;
}

}  // namespace moe::parent
