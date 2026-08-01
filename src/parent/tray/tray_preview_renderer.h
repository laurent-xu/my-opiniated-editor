#pragma once

#include <string>

#include "src/parent/tray/tray_preview_request.h"

namespace moe::parent {

class Tray;

[[nodiscard]] std::string render_tray_preview(TrayPreviewRequest const& preview,
                                              Tray const* previewed_tray);

}  // namespace moe::parent
