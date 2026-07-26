#pragma once

#include <string>

#include "src/parent/tray_number.h"

namespace moe::parent {

class TrayId {
 public:
  [[nodiscard]] static TrayId anonymous(TrayNumber number);

  [[nodiscard]] TrayNumber anonymous_number() const { return number; }
  [[nodiscard]] std::string key() const;
  [[nodiscard]] std::string label() const;

  [[nodiscard]] bool operator==(TrayId const& other) const {
    return number.value() == other.number.value();
  }
  [[nodiscard]] bool operator!=(TrayId const& other) const { return !(*this == other); }

 private:
  explicit TrayId(TrayNumber number);

  TrayNumber number;
};

}  // namespace moe::parent
