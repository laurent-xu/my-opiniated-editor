#pragma once

#include "src/base/file_descriptor.h"
#include "src/parent/pane/pane_id.h"
#include "src/parent/tray/tray_id.h"

namespace moe::parent {

struct TrayOutputSource {
  TrayId tray_id;
  base::FileDescriptor file_descriptor;
};

struct TrayPaneOutputSource {
  TrayId tray_id;
  PaneId pane_id;
  base::FileDescriptor file_descriptor;
};

}  // namespace moe::parent
