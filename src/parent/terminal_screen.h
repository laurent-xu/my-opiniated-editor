#pragma once

#include <vterm.h>

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <string_view>

#include "src/parent/content_pty_session.h"
#include "src/parent/terminal_position.h"

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
  [[nodiscard]] std::string render_region_snapshot(TerminalPosition origin) const;
  [[nodiscard]] std::string render_region_snapshot(TerminalPosition origin,
                                                   TerminalSize region_size) const;
  [[nodiscard]] static std::string render_blank_region_snapshot(TerminalPosition origin,
                                                                TerminalSize region_size);

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
  [[nodiscard]] std::string screen_row_snapshot_line(int row, bool allow_erase_to_end_of_line,
                                                     int columns) const;
  [[nodiscard]] std::string render_region_snapshot(TerminalPosition origin,
                                                   TerminalSize region_size,
                                                   bool restore_cursor) const;
  static void append_blank_region(std::string& output, TerminalPosition origin,
                                  TerminalSize region_size);
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
