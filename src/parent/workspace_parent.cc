#include "src/parent/workspace_parent.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/parent/input/command/parent_input_command.h"
#include "src/parent/input/event/parent_input_event.h"
#include "src/parent/input/parent_input_decoder.h"
#include "src/parent/overlay/path_picker_overlay.h"
#include "src/parent/runtime/parent_command_dispatcher.h"
#include "src/parent/runtime/parent_command_dispatcher_config.h"
#include "src/parent/runtime/raw_terminal_mode_guard.h"
#include "src/parent/shell/shell_configuration.h"
#include "src/parent/status/parent_status.h"
#include "src/parent/status/parent_status_serializer.h"
#include "src/parent/tray/tray_id_kind.h"
#include "src/parent/tray/tray_manager.h"
#include "src/parent/view/pane_view_protocol.h"
#include "src/parent/worktree/overlay/worktree_management_overlay.h"
#include "src/parent/worktree/registration/worktree_repository_registrar.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"
#include "src/parent/worktree/worktree_helper_commands.h"

namespace moe::parent {

using base::TerminalSize;

namespace {

constexpr TerminalSize DEFAULT_TERMINAL_SIZE{.rows = 24, .cols = 80};
constexpr int POLL_TIMEOUT_MILLISECONDS = 50;
constexpr char const* PARENT_STATUS_DESCRIPTOR_ENVIRONMENT = "MOE_PARENT_STATUS_FD";
constexpr char const* PARENT_VIEW_DESCRIPTOR_ENVIRONMENT = "MOE_PARENT_VIEW_FD";
constexpr base::FileDescriptor PARENT_INPUT_DESCRIPTOR{STDIN_FILENO};
constexpr base::FileDescriptor PARENT_OUTPUT_DESCRIPTOR{STDOUT_FILENO};
constexpr std::string_view CLEAR_TERMINAL_SURFACE = "\x1b[0m\x1b[H\x1b[2J\x1b[3J";

std::sig_atomic_t volatile stop_requested = 0;

bool has_value(char const* value) { return value != nullptr && value[0] != '\0'; }

std::optional<base::FileDescriptor> inherited_descriptor_from_environment(
    char const* const environment) {
  char const* const value = std::getenv(environment);
  if (!has_value(value)) {
    return std::nullopt;
  }

  int descriptor_value = -1;
  char const* const end = value + std::strlen(value);
  std::from_chars_result const result = std::from_chars(value, end, descriptor_value);
  if (result.ec != std::errc{} || result.ptr != end || descriptor_value < 0) {
    throw std::runtime_error(std::string("invalid ") + environment);
  }
  int const descriptor_flags = fcntl(descriptor_value, F_GETFD);
  if (descriptor_flags < 0 ||
      fcntl(descriptor_value, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0) {
    throw std::runtime_error(std::string("failed to protect ") + environment + ": " +
                             std::strerror(errno));
  }
  if (unsetenv(environment) != 0) {
    throw std::runtime_error(std::string("failed to clear ") + environment + ": " +
                             std::strerror(errno));
  }
  return base::FileDescriptor(descriptor_value);
}

void handle_stop_signal(int const signal_number) {
  static_cast<void>(signal_number);
  stop_requested = 1;
}

bool configure_terminal_environment() {
  std::string const terminal_type(terminal_type_for_child(std::getenv("TERM")));
  if (setenv("TERM", terminal_type.c_str(), 1) != 0) {
    std::cerr << "workspace_parent: failed to set TERM: " << std::strerror(errno) << '\n';
    return false;
  }
  return true;
}

TerminalSize sanitized_terminal_size(winsize const window_size) {
  if (window_size.ws_row == 0 || window_size.ws_col == 0) {
    return DEFAULT_TERMINAL_SIZE;
  }
  return {.rows = static_cast<int>(window_size.ws_row),
          .cols = static_cast<int>(window_size.ws_col)};
}

TerminalSize terminal_size_from(base::FileDescriptor const terminal) {
  winsize window_size{};
  if (terminal.is_valid() && ioctl(terminal.value(), TIOCGWINSZ, &window_size) == 0) {
    return sanitized_terminal_size(window_size);
  }
  return DEFAULT_TERMINAL_SIZE;
}

bool same_size(TerminalSize const left, TerminalSize const right) {
  return left.rows == right.rows && left.cols == right.cols;
}

pollfd readable_descriptor(base::FileDescriptor const descriptor) {
  return {.fd = descriptor.value(), .events = POLLIN, .revents = 0};
}

void write_all(base::FileDescriptor const output, std::string_view bytes) {
  while (!bytes.empty()) {
    ssize_t const written = ::write(output.value(), bytes.data(), bytes.size());
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
    }
    bytes.remove_prefix(static_cast<std::size_t>(written));
  }
}

ParentOverlayKind active_overlay_kind(TrayManager const& trays) {
  if (trays.active_worktree_management_overlay() != nullptr) {
    return ParentOverlayKind::WORKTREE_MANAGEMENT;
  }
  return ParentOverlayKind::NONE;
}

ParentPaneMode active_pane_mode(TrayManager const& trays) {
  std::optional<PaneMoveSession> const& move = trays.active_pane_move_session();
  if (move.has_value()) {
    if (move->operation() == PaneMoveOperation::SWAP) {
      return ParentPaneMode::SWAP_TARGET;
    }
    return move->stage() == PaneMoveStage::TARGET ? ParentPaneMode::MOVE_TARGET
                                                  : ParentPaneMode::MOVE_DROP;
  }
  return trays.active_pane_selection().has_value() ? ParentPaneMode::SELECTION
                                                   : ParentPaneMode::NONE;
}

std::size_t active_pane_selected_nodes(TrayManager const& trays) {
  std::optional<PaneMoveSession> const& move = trays.active_pane_move_session();
  if (move.has_value()) {
    return move->source().nodes().size();
  }
  std::optional<PaneSelection> const& selection = trays.active_pane_selection();
  return selection.has_value() ? selection->nodes().size() : 0U;
}

void publish_parent_status(TrayManager const& trays, bool const command_mode,
                           std::optional<base::FileDescriptor> const status_descriptor) {
  if (!status_descriptor.has_value()) {
    return;
  }

  WorktreeManagementOverlay const* const worktree_overlay =
      trays.active_worktree_management_overlay();
  ParentStatus const status{
      .command_mode = command_mode,
      .active_tray = trays.active_id(),
      .overlay = active_overlay_kind(trays),
      .worktree_overlay_start_row =
          worktree_overlay == nullptr
              ? std::nullopt
              : std::optional<int>(worktree_overlay->opaque_region_start_row()),
      .pane_mode = active_pane_mode(trays),
      .pane_selected_nodes = active_pane_selected_nodes(trays),
  };
  ParentPaneView const pane_view{
      .layout = trays.active_pane_layout(),
      .focused_pane = trays.active_focused_pane_id(),
      .maximized = trays.active_focused_pane_is_maximized(),
      .selection = trays.active_pane_selection(),
      .move = trays.active_pane_move_session(),
  };
  std::optional<TrayPanePreview> const preview = trays.active_worktree_management_pane_preview();
  std::string message = preview.has_value()
                            ? serialize_parent_status(status, pane_view,
                                                      ParentPanePreview{
                                                          .tray_key = preview->tray_id.key(),
                                                          .origin_row = preview->origin.row,
                                                          .origin_column = preview->origin.column,
                                                          .size = preview->size,
                                                          .layout = preview->layout,
                                                          .focused_pane = preview->focused_pane,
                                                          .maximized = preview->maximized,
                                                      })
                            : serialize_parent_status(status, pane_view);
  message.push_back('\n');
  write_all(*status_descriptor, message);
}

void write_active_surface_input(TrayManager& trays, std::string_view const bytes) {
  WorktreeManagementOverlay* const overlay = trays.active_worktree_management_overlay();
  if (overlay != nullptr) {
    overlay->write_input(bytes);
    return;
  }
  trays.write_input(bytes);
}

void redraw_active_surface(TrayManager const& trays, bool const browser_pane_view) {
  WorktreeManagementOverlay const* const overlay = trays.active_worktree_management_overlay();
  bool const overlay_over_browser_panes = browser_pane_view && overlay != nullptr;
  write_all(PARENT_OUTPUT_DESCRIPTOR,
            overlay_over_browser_panes ? CLEAR_TERMINAL_SURFACE : trays.active_redraw_output());
  if (overlay != nullptr) {
    write_all(PARENT_OUTPUT_DESCRIPTOR,
              trays.active_worktree_management_overlay_redraw_output(!overlay_over_browser_panes));
  }
}

std::filesystem::path current_parent_executable() {
  std::error_code error;
  std::filesystem::path const executable = std::filesystem::read_symlink("/proc/self/exe", error);
  if (error != std::error_code{}) {
    throw std::filesystem::filesystem_error("resolve workspace parent executable", "/proc/self/exe",
                                            error);
  }
  return executable;
}

bool route_parent_input_to_active_tray(TrayManager& trays, std::string_view const bytes,
                                       ParentInputDecoder& input_decoder,
                                       ParentCommandDispatcher& command_dispatcher,
                                       TerminalSize const size,
                                       std::optional<base::FileDescriptor> const status_descriptor,
                                       bool const browser_pane_view) {
  bool trays_destroyed = false;
  bool status_published = false;
  for (ParentInputEvent const& event : input_decoder.consume(bytes)) {
    if (LiteralInputEvent const* const literal = std::get_if<LiteralInputEvent>(&event);
        literal != nullptr) {
      if (!command_dispatcher.command_mode()) {
        write_active_surface_input(trays, literal->bytes);
      }
      continue;
    }

    ParentInputCommand const& command = std::get<CommandInputEvent>(event).command;
    ParentCommandDispatchEffects const effects = command_dispatcher.dispatch(command, size);
    if (effects.publish_status) {
      publish_parent_status(trays, command_dispatcher.command_mode(), status_descriptor);
      status_published = true;
    }
    if (effects.redraw) {
      redraw_active_surface(trays, browser_pane_view);
    }
    trays_destroyed = effects.trays_destroyed || trays_destroyed;
  }

  WorktreeManagementOverlay* const overlay = trays.active_worktree_management_overlay();
  if (overlay != nullptr) {
    if (overlay->take_full_redraw_request()) {
      if (!status_published) {
        publish_parent_status(trays, command_dispatcher.command_mode(), status_descriptor);
      }
      redraw_active_surface(trays, browser_pane_view);
    } else {
      write_all(PARENT_OUTPUT_DESCRIPTOR,
                trays.active_worktree_management_overlay_redraw_output(!browser_pane_view));
    }
  }
  return trays_destroyed;
}

bool forward_parent_input_to_active_tray(
    TrayManager& trays, ParentInputDecoder& input_decoder,
    ParentCommandDispatcher& command_dispatcher, TerminalSize const size,
    std::optional<base::FileDescriptor> const status_descriptor, bool const browser_pane_view) {
  std::array<char, 4096> buffer{};
  ssize_t const read_count = ::read(PARENT_INPUT_DESCRIPTOR.value(), buffer.data(), buffer.size());
  if (read_count <= 0) {
    if (read_count == 0 || errno == EINTR || errno == EIO) {
      return false;
    }
    throw std::runtime_error(std::string("read parent input failed: ") + std::strerror(errno));
  }

  return route_parent_input_to_active_tray(
      trays, std::string_view(buffer.data(), static_cast<std::size_t>(read_count)), input_decoder,
      command_dispatcher, size, status_descriptor, browser_pane_view);
}

void draw_tray_output(TrayManager& trays, TrayPaneOutputSource const& source,
                      std::optional<base::FileDescriptor> const view_descriptor) {
  std::optional<std::string> const output = trays.read_output(source.tray_id, source.pane_id);
  if (!output.has_value()) {
    return;
  }
  if (view_descriptor.has_value()) {
    write_all(
        *view_descriptor,
        encode_pane_view_frame(PaneViewOutput{
            .tray_key = source.tray_id.key(), .pane_id = source.pane_id, .bytes = output.value()}));
  }
  if (source.tray_id == trays.active_id() &&
      trays.active_worktree_management_overlay() == nullptr) {
    if (trays.active_pane_output_can_passthrough(source.pane_id)) {
      write_all(PARENT_OUTPUT_DESCRIPTOR, output.value());
    } else {
      write_all(PARENT_OUTPUT_DESCRIPTOR, trays.active_redraw_output());
    }
  } else if (!view_descriptor.has_value() &&
             trays.active_worktree_management_overlay_previews(source.tray_id)) {
    write_all(PARENT_OUTPUT_DESCRIPTOR, trays.active_worktree_management_overlay_redraw_output());
  }
}

void read_pane_view_input(TrayManager& trays, base::FileDescriptor const view_descriptor,
                          std::string& view_buffer) {
  std::array<char, 4096> buffer{};
  ssize_t const read_count = ::read(view_descriptor.value(), buffer.data(), buffer.size());
  if (read_count <= 0) {
    return;
  }
  view_buffer.append(buffer.data(), static_cast<std::size_t>(read_count));
  while (true) {
    std::optional<PaneViewMessage> const message = decode_pane_view_frame(view_buffer);
    if (!message.has_value()) {
      return;
    }
    if (auto const* const resize = std::get_if<PaneViewResize>(&*message); resize != nullptr) {
      static_cast<void>(
          trays.resize_pane_viewport(resize->tray_key, resize->pane_id, resize->size));
    }
  }
}

void synchronize_active_tray_size_if_changed(TrayManager& trays, TerminalSize& last_size,
                                             bool const browser_pane_view) {
  TerminalSize const current_size = terminal_size_from(PARENT_OUTPUT_DESCRIPTOR);
  if (same_size(current_size, last_size)) {
    return;
  }
  trays.resize_active(current_size);
  redraw_active_surface(trays, browser_pane_view);
  last_size = current_size;
}

}  // namespace

int run_workspace_parent() {
  stop_requested = 0;
  std::signal(SIGTERM, handle_stop_signal);
  std::signal(SIGINT, handle_stop_signal);

  if (!configure_terminal_environment()) {
    return 126;
  }

  RawTerminalModeGuard const raw_terminal(PARENT_INPUT_DESCRIPTOR);
  TerminalSize last_size = terminal_size_from(PARENT_OUTPUT_DESCRIPTOR);
  ParentInputDecoder input_decoder;
  std::optional<base::FileDescriptor> const parent_status_descriptor =
      inherited_descriptor_from_environment(PARENT_STATUS_DESCRIPTOR_ENVIRONMENT);
  std::optional<base::FileDescriptor> const parent_view_descriptor =
      inherited_descriptor_from_environment(PARENT_VIEW_DESCRIPTOR_ENVIRONMENT);
  bool const browser_pane_view = parent_view_descriptor.has_value();
  std::string parent_view_buffer;
  std::filesystem::path const parent_executable = current_parent_executable();
  std::filesystem::path const protected_worktree_path =
      std::filesystem::weakly_canonical(std::filesystem::current_path());
  std::unique_ptr<TrayManager> trays = TrayManager::start(TrayConfig{
      .command = interactive_shell_command(configured_login_shell()),
      .working_directory = configured_home_directory(),
      .initial_size = last_size,
      .estimate_layout_sizes = !parent_view_descriptor.has_value(),
  });
  ParentCommandDispatcher command_dispatcher(
      *trays, ParentCommandDispatcherConfig{
                  .parent_executable = parent_executable,
                  .protected_worktree_path = protected_worktree_path,
                  .worktree_registry_path = WorktreeRegistryStore::default_registry_path(),
                  .git_executable = configured_git_executable(),
                  .fzf_executable = configured_fzf_executable(),
              });
  publish_parent_status(*trays, command_dispatcher.command_mode(), parent_status_descriptor);

  while (stop_requested == 0) {
    if (trays->destroy_exited_trays()) {
      publish_parent_status(*trays, command_dispatcher.command_mode(), parent_status_descriptor);
      redraw_active_surface(*trays, browser_pane_view);
    }

    synchronize_active_tray_size_if_changed(*trays, last_size, browser_pane_view);

    std::vector<TrayOutputSource> overlay_sources =
        trays->worktree_management_overlay_output_sources();
    std::vector<TrayPaneOutputSource> output_sources = trays->output_sources();
    std::vector<pollfd> descriptors;
    descriptors.reserve(overlay_sources.size() + output_sources.size() + 2U);
    descriptors.push_back(readable_descriptor(PARENT_INPUT_DESCRIPTOR));
    std::optional<std::size_t> view_descriptor_index;
    if (parent_view_descriptor.has_value()) {
      view_descriptor_index = descriptors.size();
      descriptors.push_back(readable_descriptor(*parent_view_descriptor));
    }
    std::size_t const overlay_descriptor_start = descriptors.size();
    for (TrayOutputSource const& source : overlay_sources) {
      descriptors.push_back(readable_descriptor(source.file_descriptor));
    }
    std::size_t const tray_descriptor_start = descriptors.size();
    for (TrayPaneOutputSource const& source : output_sources) {
      descriptors.push_back(readable_descriptor(source.file_descriptor));
    }

    int const result = poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()),
                            POLL_TIMEOUT_MILLISECONDS);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("poll workspace parent failed: ") +
                               std::strerror(errno));
    }
    if (result == 0) {
      continue;
    }

    synchronize_active_tray_size_if_changed(*trays, last_size, browser_pane_view);

    if (view_descriptor_index.has_value() &&
        (descriptors[*view_descriptor_index].revents & POLLIN) != 0) {
      if (!parent_view_descriptor.has_value()) {
        throw std::logic_error("pane view poll index has no descriptor");
      }
      read_pane_view_input(*trays, parent_view_descriptor.value(), parent_view_buffer);
    }

    if ((descriptors[0].revents & POLLIN) != 0) {
      if (forward_parent_input_to_active_tray(*trays, input_decoder, command_dispatcher, last_size,
                                              parent_status_descriptor, browser_pane_view)) {
        continue;
      }
    }

    for (std::size_t index = 0; index < overlay_sources.size(); ++index) {
      WorktreeManagementOverlay* const overlay =
          trays->worktree_management_overlay(overlay_sources[index].tray_id);
      if (overlay == nullptr) {
        continue;
      }
      std::optional<base::FileDescriptor> const current_descriptor =
          overlay->process_file_descriptor();
      if (!current_descriptor.has_value() ||
          current_descriptor->value() != overlay_sources[index].file_descriptor.value()) {
        continue;
      }

      pollfd const& descriptor = descriptors[overlay_descriptor_start + index];
      bool changed = false;
      if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        changed = overlay->read_process_output();
      }
      changed = overlay->refresh_process_state() || changed;
      std::optional<TrayId> const tray_to_open = overlay->take_tray_to_open();
      if (tray_to_open.has_value()) {
        TrayId const source_tray = overlay_sources[index].tray_id;
        trays->clear_worktree_management_overlay(source_tray);
        try {
          if (tray_to_open->kind() == TrayIdKind::ANONYMOUS) {
            static_cast<void>(trays->switch_to(tray_to_open->anonymous_number()));
          } else {
            static_cast<void>(trays->switch_to_worktree(tray_to_open->worktree_root()));
          }
          publish_parent_status(*trays, command_dispatcher.command_mode(),
                                parent_status_descriptor);
          redraw_active_surface(*trays, browser_pane_view);
        } catch (std::exception const& error) {
          write_all(PARENT_OUTPUT_DESCRIPTOR,
                    "\r\nWorktree switch failed: " + std::string(error.what()) + "\r\n");
        }
        continue;
      }
      if (changed && overlay_sources[index].tray_id == trays->active_id()) {
        publish_parent_status(*trays, command_dispatcher.command_mode(), parent_status_descriptor);
        if (overlay->take_full_redraw_request()) {
          redraw_active_surface(*trays, browser_pane_view);
        } else {
          write_all(PARENT_OUTPUT_DESCRIPTOR,
                    trays->active_worktree_management_overlay_redraw_output(!browser_pane_view));
        }
      }
    }

    for (std::size_t index = 0; index < output_sources.size(); ++index) {
      pollfd const& descriptor = descriptors[tray_descriptor_start + index];
      if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        draw_tray_output(*trays, output_sources[index], parent_view_descriptor);
      }
    }
  }

  return 0;
}

int run_workspace_parent_command(std::span<char*> const arguments) {
  if (arguments.size() >= 2 && std::string_view(arguments[1]) == "--register-worktree-repository") {
    return run_worktree_repository_registration_command(
        arguments, {.standard_output = std::cout, .error_output = std::cerr});
  }
  if (arguments.size() >= 2 && std::string_view(arguments[1]) == "--provision-worktree") {
    return run_worktree_provision_command(
        arguments, {.standard_output = std::cout, .error_output = std::cerr});
  }
  if (arguments.size() != 1) {
    std::cerr << "usage: workspace_parent\n";
    return 2;
  }
  return run_workspace_parent();
}

int exec_configured_login_shell() {
  if (!configure_terminal_environment()) {
    return 126;
  }

  std::vector<std::string> const command = interactive_shell_command(configured_login_shell());

  std::vector<char*> argv;
  argv.reserve(command.size() + 1);
  for (std::string const& part : command) {
    argv.push_back(const_cast<char*>(part.c_str()));
  }
  argv.push_back(nullptr);

  execvp(argv[0], argv.data());
  std::cerr << "workspace_parent: failed to exec " << command.front() << ": "
            << std::strerror(errno) << '\n';
  return 127;
}

}  // namespace moe::parent
