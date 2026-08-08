#pragma once

#include <string>

#include "src/parent/status/parent_pane_view.h"
#include "src/parent/status/parent_status.h"

namespace moe::parent {

[[nodiscard]] std::string serialize_parent_status(ParentStatus const& status);
[[nodiscard]] std::string serialize_parent_status(ParentStatus const& status,
                                                  ParentPaneView const& pane_view);
[[nodiscard]] std::string serialize_parent_status(ParentStatus const& status,
                                                  ParentPaneView const& pane_view,
                                                  ParentPanePreview const& pane_preview);

}  // namespace moe::parent
