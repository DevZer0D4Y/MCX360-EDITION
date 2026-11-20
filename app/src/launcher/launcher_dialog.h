// The startup menu: pick a title update and an instance, or make a new
// instance. Drawn with the SDK's ImGui overlay before the runtime exists, so
// it works with touch and, through the gamepad poll, with a controller.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "launcher/importer.h"
#include "launcher/launcher.h"

namespace mc360::launcher {

class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  // Called when an option is chosen (and confirmed, if its core has
  // warnings). Returns an error to show, or nullopt once the game is starting;
  // the dialog then closes itself.
  using StartFn = std::function<std::optional<std::string>(const Selection&)>;

  // `opener` unpacks disc images and title update packages found in the
  // app's folder (see importer.h).
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, Launcher& launcher, Selection initial,
                 std::optional<std::string> initial_error, StartFn on_start,
                 ArchiveOpener opener);
  ~LauncherDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void Start(const Selection& selection);
  void DrawCore(const CoreStatus& status, float scale, float content_width);
  void DrawPopups(float scale);
  void DrawImports(float content_width);
  void PollImport();
  void Rescan();
  bool importing() const { return job_ != nullptr; }

  Launcher& launcher_;
  StartFn on_start_;
  Selection focus_;
  bool focus_pending_ = true;
  std::optional<Selection> confirm_;
  std::optional<std::string> pending_new_instance_;
  std::optional<std::string> error_;
  bool error_popup_requested_ = false;
  bool confirm_popup_requested_ = false;
  bool closing_ = false;
  bool touch_scrolled_ = false;

  ArchiveOpener opener_;
  std::vector<ImportCandidate> imports_;
  std::unique_ptr<ImportJob> job_;
  std::optional<std::string> pending_import_;  // path of the candidate to start
  std::string import_message_;
  bool import_message_ok_ = true;
  double next_rescan_time_ = 0.0;
  bool scroll_to_top_ = false;
};

}  // namespace mc360::launcher
