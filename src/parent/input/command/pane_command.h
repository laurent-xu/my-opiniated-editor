#pragma once

#include <cstdint>

namespace moe::parent {

enum class PaneCommandAction : std::uint8_t {
  UP,
  DOWN,
  LEFT,
  RIGHT,
  SPLIT_LEFT_TO_RIGHT,
  SPLIT_ABOVE_BELOW,
  TOGGLE_SELECTION_OR_SWAP,
  PROMOTE,
  DESCEND,
  GROW,
  SHRINK,
  EQUALIZE,
  TOGGLE_MOVE,
  CONFIRM_MOVE,
  ROTATE,
  TOGGLE_MAXIMIZE,
  CLOSE,
};

struct PaneCommand {
  PaneCommandAction action;
};

}  // namespace moe::parent
