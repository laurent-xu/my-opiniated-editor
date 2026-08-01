#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "src/base/file_descriptor.h"
#include "src/base/process_id.h"
#include "src/base/terminal_size.h"
#include "src/parent/pane/pane_config.h"
#include "src/parent/terminal/screen/terminal_position.h"
#include "src/process/process_exit_status.h"

namespace moe::parent {

class ContentPtySession;
class TerminalScreen;

class Pane {
 public:
  static std::unique_ptr<Pane> start(PaneConfig const& config);

  Pane(Pane const&) = delete;
  Pane& operator=(Pane const&) = delete;
  ~Pane();

  void write_input(std::string_view bytes) const;
  [[nodiscard]] std::optional<std::string> read_output();
  [[nodiscard]] std::string redraw_output() const;
  [[nodiscard]] std::string redraw_output_at(TerminalPosition origin) const;
  [[nodiscard]] std::string preview_output(TerminalPosition origin,
                                           base::TerminalSize region_size) const;
  void resize(base::TerminalSize size);
  [[nodiscard]] std::optional<process::ProcessExitStatus> try_wait_for_exit() noexcept;
  [[nodiscard]] base::FileDescriptor file_descriptor() const;
  [[nodiscard]] base::ProcessId child_pid() const;

 private:
  Pane(std::unique_ptr<ContentPtySession> content, base::TerminalSize size);

  std::unique_ptr<ContentPtySession> content;
  std::unique_ptr<TerminalScreen> terminal_screen;
};

}  // namespace moe::parent
