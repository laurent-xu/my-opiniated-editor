#include "src/parent/tray_number.h"

namespace moe::parent {

std::optional<TrayNumber> TrayNumber::from_int(int const value) {
  if (value < MIN_VALUE || value > MAX_VALUE) {
    return std::nullopt;
  }
  return TrayNumber(value);
}

TrayNumber TrayNumber::one() { return TrayNumber(MIN_VALUE); }

}  // namespace moe::parent
