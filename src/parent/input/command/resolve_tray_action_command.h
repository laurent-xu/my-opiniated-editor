#pragma once

#include "src/parent/input/command/confirmation_decision.h"

namespace moe::parent {

struct ResolveTrayActionCommand {
  ConfirmationDecision decision;
};

}  // namespace moe::parent
