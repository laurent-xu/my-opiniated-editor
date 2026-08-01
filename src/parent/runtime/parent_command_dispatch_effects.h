#pragma once

namespace moe::parent {

struct ParentCommandDispatchEffects {
  bool publish_status = false;
  bool redraw = false;
  bool trays_destroyed = false;
};

}  // namespace moe::parent
