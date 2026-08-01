#pragma once

#include "src/parent/input/command/tray_action_intent.h"

namespace moe::parent {

struct BeginTrayActionCommand {
  TrayActionIntent action;
};

}  // namespace moe::parent
