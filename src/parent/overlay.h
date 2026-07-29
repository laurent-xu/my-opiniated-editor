#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "src/base/file_descriptor.h"
#include "src/parent/content_pty_session.h"

namespace moe::parent {

class Overlay {
 public:
  Overlay() = default;
  Overlay(Overlay const&) = delete;
  Overlay& operator=(Overlay const&) = delete;
  virtual ~Overlay() = default;

  virtual void write_input(std::string_view bytes) = 0;
  [[nodiscard]] virtual bool read_process_output() = 0;
  [[nodiscard]] virtual bool refresh_process_state() = 0;
  virtual void resize(TerminalSize size) = 0;
  [[nodiscard]] virtual std::optional<base::FileDescriptor> process_file_descriptor() const = 0;
  [[nodiscard]] virtual std::string redraw_output() const = 0;
};

}  // namespace moe::parent
