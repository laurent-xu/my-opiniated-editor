#pragma once

#include "src/parent/input/tray_action_intent.h"

namespace moe::parent {

struct BeginTrayActionCommand {
  TrayActionIntent action;
};

}  // namespace moe::parent
