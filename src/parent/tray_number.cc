#include "src/parent/tray_number.h"

namespace moe::parent {
namespace {

constexpr int ONLY_AVAILABLE_TRAY = 1;

}  // namespace

std::optional<TrayNumber> TrayNumber::from_int(int const value) {
  if (value != ONLY_AVAILABLE_TRAY) {
    return std::nullopt;
  }
  return TrayNumber(value);
}

TrayNumber TrayNumber::one() { return TrayNumber(ONLY_AVAILABLE_TRAY); }

}  // namespace moe::parent
