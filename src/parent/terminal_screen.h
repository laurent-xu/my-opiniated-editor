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
  void ingest_complete_input(std::string_view bytes);
  void feed_input_to_vterm(std::string_view bytes);
  void record_erase_for_snapshot(char command, VTermPos cursor, int mode);
  [[nodiscard]] std::string screen_row_snapshot_line(int row) const;
  [[nodiscard]] static std::string cursor_position_sequence(int row, int col);
  [[nodiscard]] static VTermScreenCallbacks const& screen_callbacks();
  static int move_rect_callback(VTermRect dest, VTermRect src, void* user);
  static int settermprop_callback(VTermProp prop, VTermValue* value, void* user);
  static int push_scrollback_line_callback(int cols, VTermScreenCell const* cells, void* user);
  static int clear_scrollback_callback(void* user);

  TerminalSize screen_size;
  std::unique_ptr<VTerm, VTermDeleter> terminal;
  VTermScreen* screen = nullptr;
  VTermState* state = nullptr;
  std::string pending_utf8_bytes;
  std::string pending_snapshot_control_bytes;
  bool alternate_screen_active = false;
  bool reverse_screen_active = false;
  bool cursor_visible = true;
  std::deque<std::string> scrollback_lines;

  struct LineFillTracker;
  std::unique_ptr<LineFillTracker> line_fill_tracker;
};

}  // namespace moe::parent
