#pragma once

#include <vterm.h>

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <string_view>

#include "src/parent/content_pty_session.h"

namespace moe::parent {

class TerminalScreen {
 public:
  explicit TerminalScreen(TerminalSize size);

  TerminalScreen(TerminalScreen const&) = delete;
  TerminalScreen& operator=(TerminalScreen const&) = delete;
  TerminalScreen(TerminalScreen&& other) noexcept;
  TerminalScreen& operator=(TerminalScreen&& other) noexcept;
  ~TerminalScreen();

  void ingest(std::string_view bytes);
  void resize(TerminalSize size);
  [[nodiscard]] std::string render_snapshot() const;

 private:
  struct VTermDeleter {
    void operator()(VTerm* terminal) const noexcept;
  };

  void configure_screen_callbacks();
  void push_scrollback_line(int cols, void const* cells);
  void clear_scrollback();
  [[nodiscard]] std::string screen_row_text(int row) const;
  [[nodiscard]] static std::string cursor_position_sequence(int row, int col);
  [[nodiscard]] static VTermScreenCallbacks const& screen_callbacks();
  static int push_scrollback_line_callback(int cols, VTermScreenCell const* cells, void* user);
  static int clear_scrollback_callback(void* user);

  TerminalSize screen_size;
  std::unique_ptr<VTerm, VTermDeleter> terminal;
  VTermScreen* screen = nullptr;
  VTermState* state = nullptr;
  std::deque<std::string> scrollback_lines;
};

}  // namespace moe::parent
