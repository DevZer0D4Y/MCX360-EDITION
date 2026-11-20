#include "launcher/launcher_dialog.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <imgui.h>

#include "launcher/importer.h"
#include "launcher/xex_info.h"

namespace mc360::launcher {

namespace {

constexpr const char* kConfirmPopup = "Start anyway?";
constexpr const char* kErrorPopup = "Couldn't start";

const ImVec4 kTitleColor(0.62f, 0.85f, 0.42f, 1.0f);
const ImVec4 kDimColor(0.65f, 0.65f, 0.68f, 1.0f);
const ImVec4 kWarningColor(1.0f, 0.78f, 0.30f, 1.0f);
const ImVec4 kErrorColor(1.0f, 0.42f, 0.38f, 1.0f);
const ImVec4 kReadyColor(0.55f, 0.85f, 0.45f, 1.0f);

std::string InstanceLabel(int number) {
  return "Instance " + std::to_string(number);
}

// Folder paths shown to the player, relative to the app's Documents folder.
std::string Relative(const std::filesystem::path& path, const std::filesystem::path& base) {
  std::error_code ec;
  auto relative = std::filesystem::relative(path, base, ec);
  if (ec || relative.empty() || *relative.begin() == "..") {
    return path.string();
  }
  return relative.generic_string() + "/";
}

}  // namespace

LauncherDialog::LauncherDialog(rex::ui::ImGuiDrawer* drawer, Launcher& launcher,
                               Selection initial, std::optional<std::string> initial_error,
                               StartFn on_start, ArchiveOpener opener)
    : ImGuiDialog(drawer),
      launcher_(launcher),
      on_start_(std::move(on_start)),
      focus_(std::move(initial)),
      error_(std::move(initial_error)),
      opener_(std::move(opener)) {
  error_popup_requested_ = error_.has_value();
  imports_ = FindImportCandidates(launcher_.base_dir(), launcher_.core_infos());
}

// Waits for an import still running (the app is closing anyway).
LauncherDialog::~LauncherDialog() = default;

void LauncherDialog::Rescan() {
  launcher_.Scan();
  imports_ = FindImportCandidates(launcher_.base_dir(), launcher_.core_infos());
}

void LauncherDialog::PollImport() {
  if (job_ && job_->finished()) {
    import_message_ = job_->message();
    import_message_ok_ = job_->succeeded();
    job_.reset();  // joins the finished thread
    Rescan();
    if (HasPendingImports(imports_)) {
      // More to unpack: keep the list where the Import buttons are.
      scroll_to_top_ = true;
    } else {
      // All set: on to the game, which may only now be startable.
      focus_ = launcher_.DefaultSelection();
      focus_pending_ = true;
    }
  }
}

void LauncherDialog::Start(const Selection& selection) {
  focus_ = selection;
  auto error = on_start_(selection);
  if (error) {
    error_ = std::move(error);
    error_popup_requested_ = true;
    return;
  }
  closing_ = true;
  Close();
}

void LauncherDialog::OnDraw(ImGuiIO& io) {
  if (closing_) {
    return;
  }
  PollImport();
  // Files added in the Files app while the menu is up show up by themselves.
  if (!importing() && ImGui::GetTime() >= next_rescan_time_) {
    if (next_rescan_time_ > 0.0) {
      Rescan();
    }
    next_rescan_time_ = ImGui::GetTime() + 2.0;
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
      !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    touch_scrolled_ = false;
  }
  // The font is a small bitmap; scale it so text and buttons are a
  // comfortable size: 2x on a phone held sideways (about 844x390 points),
  // 3x on an iPad. Whole steps keep the pixel font even.
  const float scale = std::clamp(
      std::round(std::min(io.DisplaySize.x / 400.0f, io.DisplaySize.y / 185.0f)), 1.0f, 4.0f);
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushFont(nullptr, style.FontSizeBase * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 8.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * scale, 5.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * scale, 5.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.09f, 1.0f));

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("##mc360_launcher", nullptr, flags)) {
    ImGui::TextColored(kTitleColor, "MCX360 Edition");
    ImGui::SameLine();
    ImGui::TextColored(kDimColor, " Choose a title update and an instance");
    ImGui::Separator();

    const float footer_height = ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y;
    if (ImGui::BeginChild("##cores", ImVec2(0.0f, -footer_height),
                          ImGuiChildFlags_NavFlattened)) {
      const float content_width = ImGui::GetContentRegionAvail().x;
      if (scroll_to_top_) {
        ImGui::SetScrollY(0.0f);
        scroll_to_top_ = false;
      }
      DrawImports(content_width);
      if (launcher_.cores().empty()) {
        ImGui::TextColored(kErrorColor, "This build contains no title updates.");
      }
      for (const CoreStatus& status : launcher_.cores()) {
        DrawCore(status, scale, content_width);
      }
      // ImGui scrolls with a wheel or the scrollbar; on a touch screen the
      // list should follow the finger. A press that turned into a scroll
      // doesn't count as a tap when it is released.
      if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
          ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f * scale)) {
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
        touch_scrolled_ = true;
      }
    }
    ImGui::EndChild();

    ImGui::TextColored(kDimColor, "%s",
                       importing() ? "Keep the app open until unpacking finishes."
                                   : "Each instance keeps its own worlds and settings.");
    DrawPopups(scale);
  }
  ImGui::End();

  ImGui::PopStyleColor();
  ImGui::PopStyleVar(5);
  ImGui::PopFont();
  focus_pending_ = false;

  if (pending_import_ && !closing_ && !importing()) {
    const std::string path = std::move(*pending_import_);
    pending_import_.reset();
    for (const ImportCandidate& candidate : imports_) {
      if (candidate.path.string() == path && candidate.state == ImportCandidate::State::kReady) {
        import_message_.clear();
        job_ = std::make_unique<ImportJob>(candidate, launcher_.base_dir(),
                                           launcher_.core_infos(), opener_);
        job_->Start();
        break;
      }
    }
  }

  if (pending_new_instance_ && !closing_) {
    const std::string core_id = std::move(*pending_new_instance_);
    pending_new_instance_.reset();
    if (auto number = launcher_.CreateInstance(core_id)) {
      focus_ = {core_id, *number};
      focus_pending_ = true;
    } else {
      error_ = "Couldn't create a folder for a new instance.";
      error_popup_requested_ = true;
    }
  }
}

void LauncherDialog::DrawImports(float content_width) {
  // Files already unpacked aren't listed; the message after an import says
  // the original can be deleted.
  const bool any_open = std::any_of(imports_.begin(), imports_.end(), [](const auto& c) {
    return c.state != ImportCandidate::State::kInstalled;
  });
  if (!any_open && import_message_.empty()) {
    return;
  }
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::Spacing();
  ImGui::Text("Files to set up");
  ImGui::PushTextWrapPos(content_width);
  if (!import_message_.empty()) {
    ImGui::TextColored(import_message_ok_ ? kReadyColor : kErrorColor, "%s",
                       import_message_.c_str());
  }
  const ImVec2 button_size(ImGui::CalcTextSize("+ New instance").x + style.FramePadding.x * 4.0f,
                           ImGui::GetFrameHeight() * 1.3f);
  for (const ImportCandidate& candidate : imports_) {
    if (candidate.state == ImportCandidate::State::kInstalled) {
      continue;
    }
    const std::string id = candidate.path.string();
    ImGui::PushID(id.c_str());
    ImGui::Spacing();
    const bool running = job_ && job_->candidate().path == candidate.path;
    const bool ready = candidate.state == ImportCandidate::State::kReady;

    // [Import]  What it is
    //           file name, size, and any note
    if (ready && !running) {
      ImGui::BeginDisabled(importing());
      const bool import = ImGui::Button("Import", button_size);
      ImGui::EndDisabled();
      if (import && !touch_scrolled_) {
        // Started after this frame, outside the list being drawn.
        pending_import_ = id;
      }
      ImGui::SameLine();
    }
    ImGui::BeginGroup();
    std::string label;
    if (candidate.kind == ImportCandidate::Kind::kDiscImage) {
      label = "Disc image";
    } else {
      label = "Title update";
      if (candidate.version) {
        label += " " + FormatXexVersion(candidate.version);
      }
      if (candidate.core) {
        label += std::string(" (") + candidate.core->name + ")";
      }
    }
    ImGui::Text("%s", label.c_str());
    ImGui::TextColored(kDimColor, "%s, %s", candidate.path.filename().string().c_str(),
                       FormatBytes(candidate.file_size).c_str());
    if (ready && !running && candidate.kind == ImportCandidate::Kind::kTitleUpdate &&
        !candidate.core) {
      ImGui::TextColored(kWarningColor,
                         "This copy of the app can't play it yet; it can be unpacked now.");
    } else if (!ready) {
      ImGui::TextColored(candidate.state == ImportCandidate::State::kInstalled ? kReadyColor
                                                                                : kWarningColor,
                         "%s", candidate.note.c_str());
    }
    ImGui::EndGroup();

    if (running) {
      const uint64_t total = job_->bytes_total();
      const float fraction = total ? float(double(job_->bytes_done()) / double(total)) : 0.0f;
      const std::string overlay =
          std::to_string(int(fraction * 100.0f)) + "%  " + job_->current_file();
      ImGui::ProgressBar(fraction, ImVec2(content_width, ImGui::GetFrameHeight() * 1.3f),
                         overlay.c_str());
    }
    ImGui::PopID();
  }
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  ImGui::Separator();
}

void LauncherDialog::DrawCore(const CoreStatus& status, float scale, float content_width) {
  const CoreInfo& core = *status.core;
  ImGui::PushID(core.id);

  ImGui::Spacing();
  ImGui::Text("%s", core.name);
  if (core.identity_known) {
    ImGui::SameLine();
    ImGui::TextColored(kDimColor, "  v%s", FormatXexVersion(core.game_version).c_str());
  }
  ImGui::SameLine();
  if (status.has_error()) {
    ImGui::TextColored(kErrorColor, "  Can't start");
  } else if (status.has_warning()) {
    ImGui::TextColored(kWarningColor, "  Check files");
  } else {
    ImGui::TextColored(kReadyColor, "  Ready");
  }

  ImGui::PushTextWrapPos(content_width);
  for (const Issue& issue : status.issues) {
    ImGui::TextColored(issue.severity == Issue::Severity::kError ? kErrorColor : kWarningColor,
                       "%s", issue.text.c_str());
  }
  ImGui::PopTextWrapPos();

  // Where everything lives, so files can be found in the Files app.
  std::string folders = "Title update: ";
  folders += status.update_root.empty() ? std::string("none")
                                        : Relative(status.update_root, launcher_.base_dir());
  folders += "   Saves: ";
  for (size_t i = 0; i < status.instances.size(); ++i) {
    folders += (i ? ", " : "") + Relative(status.instances[i].user_data_root, launcher_.base_dir());
  }
  ImGui::PushTextWrapPos(content_width);
  ImGui::TextColored(kDimColor, "%s", folders.c_str());
  ImGui::PopTextWrapPos();

  // One large button per instance, wrapping onto new rows.
  const ImGuiStyle& style = ImGui::GetStyle();
  // Four to a row, never narrower than the longest label.
  const float min_width = ImGui::CalcTextSize("+ New instance").x + style.FramePadding.x * 4.0f;
  const ImVec2 button_size(
      std::max(min_width, (content_width - 3.0f * style.ItemSpacing.x) / 4.0f - 1.0f),
      ImGui::GetFrameHeight() * 1.3f);
  float row_x = 0.0f;
  const bool can_start = !status.has_error();
  for (const Instance& instance : status.instances) {
    if (row_x > 0.0f && row_x + button_size.x > content_width) {
      row_x = 0.0f;
    } else if (row_x > 0.0f) {
      ImGui::SameLine();
    }
    const Selection selection{core.id, instance.number};
    ImGui::BeginDisabled(!can_start || importing());
    const std::string label = InstanceLabel(instance.number);
    if (ImGui::Button(label.c_str(), button_size) && !touch_scrolled_) {
      if (status.has_warning()) {
        confirm_ = selection;
        confirm_popup_requested_ = true;
      } else {
        Start(selection);
      }
    }
    ImGui::EndDisabled();
    if (focus_pending_ && selection == focus_ && can_start && !importing()) {
      ImGui::SetItemDefaultFocus();
      ImGui::SetKeyboardFocusHere(-1);
      // Highlighted from the start, so A on a controller plays the last-used
      // instance right away (ImGui ignores A while the highlight is hidden).
      // Touching the screen hides it again.
      ImGui::SetNavCursorVisible(true);
    }
    row_x += button_size.x + style.ItemSpacing.x;
  }
  if (row_x > 0.0f && row_x + button_size.x <= content_width) {
    ImGui::SameLine();
  }
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.22f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.29f, 0.32f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.36f, 0.40f, 1.0f));
  ImGui::BeginDisabled(importing());
  const bool new_instance = ImGui::Button("+ New instance", button_size);
  ImGui::EndDisabled();
  if (new_instance && !touch_scrolled_) {
    // Creating it rescans the cores this frame is iterating over; do it after.
    pending_new_instance_ = core.id;
  }
  ImGui::PopStyleColor(3);

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::PopID();
}

void LauncherDialog::DrawPopups(float scale) {
  const ImGuiIO& io = ImGui::GetIO();
  const ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  const float popup_width = std::min(io.DisplaySize.x * 0.8f, 520.0f * scale);
  const ImVec2 button_size(popup_width * 0.4f, ImGui::GetFrameHeight() * 1.5f);

  if (confirm_popup_requested_) {
    ImGui::OpenPopup(kConfirmPopup);
    confirm_popup_requested_ = false;
  }
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(popup_width, 0.0f));
  if (ImGui::BeginPopupModal(kConfirmPopup, nullptr,
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings)) {
    const CoreStatus* status = confirm_ ? launcher_.FindCore(confirm_->core_id) : nullptr;
    ImGui::PushTextWrapPos(0.0f);
    if (status) {
      ImGui::Text("%s may not run correctly:", status->core->name);
      for (const Issue& issue : status->issues) {
        ImGui::TextColored(kWarningColor, "%s", issue.text.c_str());
      }
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    bool close = false;
    if (ImGui::Button("Back", button_size)) {
      close = true;
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Start anyway", button_size) && confirm_) {
      const Selection selection = *confirm_;
      ImGui::CloseCurrentPopup();
      confirm_.reset();
      ImGui::EndPopup();
      Start(selection);
      return;
    }
    if (close || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
      confirm_.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (error_popup_requested_) {
    ImGui::OpenPopup(kErrorPopup);
    error_popup_requested_ = false;
  }
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(popup_width, 0.0f));
  if (ImGui::BeginPopupModal(kErrorPopup, nullptr,
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kErrorColor, "%s", error_ ? error_->c_str() : "");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("OK", button_size) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
      error_.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
  }
}

}  // namespace mc360::launcher
