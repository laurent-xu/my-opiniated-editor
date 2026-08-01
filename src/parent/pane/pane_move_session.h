#pragma once

#include <cstdint>
#include <optional>

#include "src/parent/pane/pane_geometry.h"
#include "src/parent/pane/pane_move.h"
#include "src/parent/pane/pane_navigation.h"

namespace moe::parent {

enum class PaneMoveStage : std::uint8_t {
  TARGET,
  DROP,
};

enum class PaneMoveOperation : std::uint8_t {
  MOVE,
  SWAP,
};

class PaneMoveSession {
 public:
  [[nodiscard]] static PaneMoveSession begin(PaneSelection source);

  [[nodiscard]] PaneSelection const& source() const;
  [[nodiscard]] std::optional<PaneNodeId> target() const;
  [[nodiscard]] PaneMoveStage stage() const;
  [[nodiscard]] PaneMoveOperation operation() const;
  [[nodiscard]] std::optional<PaneDropDirection> drop_direction() const;

  [[nodiscard]] bool step_target(PaneLayout const& layout, PaneGeometry const& geometry,
                                 PaneFocusDirection direction);
  [[nodiscard]] bool promote_target(PaneLayout const& layout);
  [[nodiscard]] bool descend_target(PaneLayout const& layout);
  [[nodiscard]] bool toggle_swap();
  [[nodiscard]] bool lock_target();
  [[nodiscard]] bool set_drop_direction(PaneDropDirection direction);
  [[nodiscard]] std::optional<PaneLayout> preview(PaneLayout const& layout) const;
  [[nodiscard]] bool confirm(PaneLayout& layout) const;

 private:
  explicit PaneMoveSession(PaneSelection source);

  [[nodiscard]] bool target_is_eligible(PaneLayout const& layout, PaneNodeId candidate) const;

  PaneSelection source_selection;
  std::optional<PaneNodeId> target_node;
  PaneMoveStage move_stage{PaneMoveStage::TARGET};
  PaneMoveOperation move_operation{PaneMoveOperation::MOVE};
  std::optional<PaneDropDirection> selected_drop_direction;
};

}  // namespace moe::parent
