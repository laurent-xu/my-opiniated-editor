#include "src/parent/worktree/overlay/terminal_text_field.h"

#include <string>

#include "gtest/gtest.h"

namespace moe::parent {

TEST(TerminalTextFieldTest, InsertsBytesAtTheCursor) {
  TerminalTextField field;
  field.insert('a');
  field.insert('c');
  field.move_cursor_left();
  field.insert('b');

  EXPECT_EQ(field.value(), "abc");
  EXPECT_EQ(field.cursor_offset(), 2U);
}

TEST(TerminalTextFieldTest, CursorMovementStopsAtAsciiBoundaries) {
  TerminalTextField field;
  field.set_value("ab");

  field.move_cursor_right();
  EXPECT_EQ(field.cursor_offset(), 2U);
  field.move_cursor_left();
  field.move_cursor_left();
  field.move_cursor_left();
  EXPECT_EQ(field.cursor_offset(), 0U);
  field.move_cursor_right();
  EXPECT_EQ(field.cursor_offset(), 1U);
}

TEST(TerminalTextFieldTest, CursorMovementTreatsUtf8CodePointAsOneCharacter) {
  TerminalTextField field;
  std::string const value = "a\xE2\x82\xACz";
  field.set_value(value);

  field.move_cursor_left();
  EXPECT_EQ(field.cursor_offset(), 4U);
  field.move_cursor_left();
  EXPECT_EQ(field.cursor_offset(), 1U);
  field.move_cursor_right();
  EXPECT_EQ(field.cursor_offset(), 4U);
}

TEST(TerminalTextFieldTest, BackspaceErasesCodePointBeforeCursor) {
  TerminalTextField field;
  field.set_value("a\xE2\x82\xACz");
  field.move_cursor_left();
  field.backspace();

  EXPECT_EQ(field.value(), "az");
  EXPECT_EQ(field.cursor_offset(), 1U);
  field.move_cursor_to_start();
  field.backspace();
  EXPECT_EQ(field.value(), "az");
  EXPECT_EQ(field.cursor_offset(), 0U);
}

TEST(TerminalTextFieldTest, DeleteErasesCodePointAtCursor) {
  TerminalTextField field;
  field.set_value("a\xE2\x82\xACz");
  field.move_cursor_to_start();
  field.move_cursor_right();
  field.delete_at_cursor();

  EXPECT_EQ(field.value(), "az");
  EXPECT_EQ(field.cursor_offset(), 1U);
  field.move_cursor_to_end();
  field.delete_at_cursor();
  EXPECT_EQ(field.value(), "az");
  EXPECT_EQ(field.cursor_offset(), 2U);
}

TEST(TerminalTextFieldTest, ClearAndSetValueKeepCursorConsistent) {
  TerminalTextField field;
  field.set_value("branch");
  EXPECT_EQ(field.cursor_offset(), 6U);

  field.clear();
  EXPECT_TRUE(field.value().empty());
  EXPECT_EQ(field.cursor_offset(), 0U);
}

}  // namespace moe::parent
