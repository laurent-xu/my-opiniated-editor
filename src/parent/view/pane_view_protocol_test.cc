#include "src/parent/view/pane_view_protocol.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include "gtest/gtest.h"

namespace {

moe::parent::PaneId pane_id(std::uint64_t const value) {
  std::optional<moe::parent::PaneId> const result = moe::parent::PaneId::from_value(value);
  if (!result.has_value()) {
    throw std::logic_error("test pane id must be nonzero");
  }
  return *result;
}

TEST(PaneViewProtocolTest, RoundTripsRawPaneOutputAcrossPartialReads) {
  std::string const bytes("one\0two\xff", 8);
  std::string frame = moe::parent::encode_pane_view_frame(moe::parent::PaneViewOutput{
      .tray_key = "anonymous:1", .pane_id = pane_id(7), .bytes = bytes});
  std::string buffer = frame.substr(0, 5);
  EXPECT_FALSE(moe::parent::decode_pane_view_frame(buffer).has_value());

  buffer.append(frame.substr(5));
  std::optional<moe::parent::PaneViewMessage> const decoded =
      moe::parent::decode_pane_view_frame(buffer);

  if (!decoded.has_value()) {
    FAIL() << "pane output was not decoded";
    return;
  }
  auto const& output = std::get<moe::parent::PaneViewOutput>(decoded.value());
  EXPECT_EQ(output.tray_key, "anonymous:1");
  EXPECT_EQ(output.pane_id, pane_id(7));
  EXPECT_EQ(output.bytes, bytes);
  EXPECT_TRUE(buffer.empty());
}

TEST(PaneViewProtocolTest, BrowserResizePayloadUsesTheSamePaneIdentity) {
  moe::parent::PaneViewOutput const output{
      .tray_key = "worktree:/tmp/repo",
      .pane_id = pane_id(42),
      .bytes = std::string(8, '\0'),
  };
  std::string payload = moe::parent::encode_pane_output_payload(output);
  payload.resize(payload.size() - output.bytes.size());
  payload.push_back('\0');
  payload.push_back('\0');
  payload.push_back('\0');
  payload.push_back('\x18');
  payload.push_back('\0');
  payload.push_back('\0');
  payload.push_back('\0');
  payload.push_back('\x50');

  std::optional<moe::parent::PaneViewResize> const resize =
      moe::parent::decode_pane_resize_payload(payload);

  if (!resize.has_value()) {
    FAIL() << "pane resize was not decoded";
    return;
  }
  EXPECT_EQ(resize.value().tray_key, output.tray_key);
  EXPECT_EQ(resize.value().pane_id, output.pane_id);
  EXPECT_EQ(resize.value().size.rows, 24);
  EXPECT_EQ(resize.value().size.cols, 80);
}

}  // namespace
