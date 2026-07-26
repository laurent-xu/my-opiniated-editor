#include "src/parent/tray_id.h"

namespace moe::parent {

TrayId TrayId::anonymous(TrayNumber const number) { return TrayId(number); }

std::string TrayId::key() const { return "anonymous:" + std::to_string(number.value()); }

std::string TrayId::label() const { return "tray " + std::to_string(number.value()); }

TrayId::TrayId(TrayNumber const tray_number) : number(tray_number) {}

}  // namespace moe::parent
