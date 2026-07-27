#pragma once

#include "src/base/file_descriptor.h"
#include "src/parent/tray_id.h"

namespace moe::parent {

struct TrayOutputSource {
  TrayId tray_id;
  base::FileDescriptor file_descriptor;
};

}  // namespace moe::parent
