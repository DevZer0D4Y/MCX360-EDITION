#include "mc360_app.h"

#include <cstdlib>
#include <system_error>
#include <variant>

#include <rex/cvar.h>
#include <rex/input/touch/touch_controls.h>
#include <rex/kernel/xam/module.h>
#include <rex/logging.h>
#include <rex/system.h>

#include "launcher/core_loader.h"
#include "launcher/gamepad_poll.h"
#include "launcher/launcher_dialog.h"
#include "launcher/xex_info.h"

REXCVAR_DEFINE_STRING(launcher_menu, "auto", "Launcher",
                      "Title update menu at launch: \"auto\" shows it when there is a choice to "
                      "make or a problem with the files, \"always\" shows it every time");

using mc360::launcher::CoreStatus;
using mc360::launcher::Issue;
using mc360::launcher::Launcher;
using mc360::launcher::Selection;

namespace {

void LogScan(const Launcher& launcher) {
  REXLOG_INFO("Launcher: {} core(s) in this build, folders in {}", launcher.cores().size(),
              launcher.base_dir().string());
  for (const CoreStatus& status : launcher.cores()) {
    const auto& core = *status.core;
    REXLOG_INFO("Launcher:   {} ({}): version {}, update folder '{}', {} instance(s)", core.id,
                core.name,
                core.identity_known ? mc360::launcher::FormatXexVersion(core.game_version)
                                    : std::string("unknown"),
                status.update_root.string(), status.instances.size());
    for (const Issue& issue : status.issues) {
      REXLOG_WARN("Launcher:     {}: {}",
                  issue.severity == Issue::Severity::kError ? "error" : "warning", issue.text);
    }
  }
}

}  // namespace

std::filesystem::path Mc360App::LauncherBaseDir(const rex::PathConfig& defaults) const {
#if MC360_IOS
  // Everything the player manages lives in Documents, which the Files app shows.
  if (const char* home = std::getenv("HOME"); home && home[0]) {
    return std::filesystem::path(home) / "Documents";
  }
#endif
  // Desktop: the folder that holds the game folder.
  if (!defaults.game_data_root.empty()) {
    return std::filesystem::absolute(defaults.game_data_root).parent_path();
  }
  std::error_code ec;
  return std::filesystem::current_path(ec);
}

std::optional<rex::PathConfig> Mc360App::OnFinalizePaths(
    const rex::PathConfig& defaults, std::function<void(rex::PathConfig)> resume) {
  launcher_ = std::make_unique<Launcher>(LauncherBaseDir(defaults),
                                         mc360::launcher::BundledCores());
  if (!defaults.game_data_root.empty()) {
    launcher_->SetGameDir(defaults.game_data_root);
  }
  // A title update folder passed explicitly (--update_data_root on desktop;
  // iOS has no command line) is used for whichever core starts.
  launcher_->SetUpdateRootOverride(defaults.update_data_root);
  launcher_->Scan();
  LogScan(*launcher_);

  const bool always = REXCVAR_GET(launcher_menu) == "always";
  const Selection initial = launcher_->DefaultSelection();
  std::optional<std::string> error;
  if (!launcher_->ShouldShowMenu(always) || !imgui_drawer()) {
    // Nothing to choose: start right away, exactly as the single-build app did.
    rex::PathConfig paths;
    error = PrepareLaunch(initial, defaults, paths);
    if (!error) {
      return paths;
    }
  }

  if (!imgui_drawer()) {
    // No overlay to draw a menu with (no graphics); all that is left is to say why.
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, *error);
    app_context().CallInUIThreadDeferred([this]() { app_context().QuitFromUIThread(); });
    return std::nullopt;
  }
  ShowLauncherMenu(defaults, std::move(resume), initial, std::move(error));
  return std::nullopt;
}

std::optional<std::string> Mc360App::PrepareLaunch(const Selection& selection,
                                                   const rex::PathConfig& defaults,
                                                   rex::PathConfig& paths) {
  const CoreStatus* status = launcher_->FindCore(selection.core_id);
  if (!status) {
    return std::string("This build contains no title updates.");
  }
  for (const Issue& issue : status->issues) {
    if (issue.severity == Issue::Severity::kError) {
      return issue.text;
    }
  }
  const auto launch_paths = launcher_->PathsFor(selection);
  if (!launch_paths) {
    return std::string("That instance no longer exists.");
  }

  auto loaded = mc360::launcher::LoadCore(*status->core);
  if (auto* load_error = std::get_if<std::string>(&loaded)) {
    return *load_error;
  }
  const mc360::CoreExports* exports = std::get<const mc360::CoreExports*>(loaded);

  paths = defaults;
  paths.update_data_root = launch_paths->update_data_root;
  paths.user_data_root = launch_paths->user_data_root;
  paths.cache_root = launch_paths->cache_root;
  std::error_code ec;
  std::filesystem::create_directories(paths.user_data_root, ec);
  std::filesystem::create_directories(paths.cache_root, ec);

  SetImageInfo(*exports->image_info);
  launcher_->SaveLastSelection(selection);
  REXLOG_INFO("Launcher: starting {} instance {} (update={} user={} cache={})",
              selection.core_id, selection.instance, paths.update_data_root.string(),
              paths.user_data_root.string(), paths.cache_root.string());
  return std::nullopt;
}

void Mc360App::ShowLauncherMenu(const rex::PathConfig& defaults,
                                std::function<void(rex::PathConfig)> resume, Selection initial,
                                std::optional<std::string> error) {
  // The on-screen gamepad would cover the menu and take its taps; it comes
  // back, set up as ReXApp does, once a choice is made.
  rex::input::touch::RemoveTouchControls();
  imgui_drawer()->SetGamepadPoll(&mc360::launcher::PollLauncherGamepad);

  // The dialog owns itself and is deleted when it closes.
  new mc360::launcher::LauncherDialog(
      imgui_drawer(), *launcher_, std::move(initial), std::move(error),
      [this, defaults, resume = std::move(resume)](
          const Selection& selection) -> std::optional<std::string> {
        rex::PathConfig paths;
        if (auto failure = PrepareLaunch(selection, defaults, paths)) {
          return failure;
        }
        imgui_drawer()->SetGamepadPoll(nullptr);
        rex::input::touch::TouchControlsHooks touch_hooks;
        touch_hooks.is_system_ui_active = [] { return rex::kernel::xam::xeXamIsUIActive(); };
        rex::input::touch::InstallTouchControls(window(), std::move(touch_hooks));
        // Not from inside the menu's draw: the runtime is built on the next
        // turn of the UI loop.
        app_context().CallInUIThreadDeferred(
            [resume, paths = std::move(paths)]() { resume(paths); });
        return std::nullopt;
      });
}
