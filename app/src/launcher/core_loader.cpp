#include "launcher/core_loader.h"

#include <cstring>
#include <filesystem>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/platform/dynlib.h>

namespace mc360::launcher {

std::variant<const CoreExports*, std::string> LoadCore(const CoreInfo& core) {
  // Loaded once and never unloaded (not even by static destructors at exit,
  // while guest threads may still be running its code); a second LoadCore for
  // the same core returns the same library.
  static auto& library = *new rex::platform::DynamicLibrary();
  static auto& loaded_id = *new std::string();
  if (library && loaded_id != core.id) {
    return std::string("Another title update is already loaded. Close the app and open it again.");
  }

  if (!library) {
    // Inside an iOS app everything that is loaded has to sit in Frameworks/ to
    // be covered by the app's signature; desktop builds keep it next to the
    // executable.
    const std::filesystem::path exe_dir = rex::filesystem::GetExecutableFolder();
    const std::filesystem::path candidates[] = {exe_dir / "Frameworks" / core.library,
                                                exe_dir / core.library};
    std::filesystem::path path;
    for (const auto& candidate : candidates) {
      std::error_code ec;
      if (std::filesystem::is_regular_file(candidate, ec)) {
        path = candidate;
        break;
      }
    }
    if (path.empty()) {
      REXLOG_ERROR("Launcher: core library {} is missing from the app", core.library);
      return std::string(core.name) + " is missing from this copy of the app (" + core.library +
             "). Reinstall the app.";
    }
    // Immediate binding, so a broken library fails here rather than mid-game.
    if (!library.Load(path, rex::platform::SymbolResolution::kImmediate)) {
      REXLOG_ERROR("Launcher: failed to load {}", path.string());
      return std::string(core.name) + " couldn't be loaded. Reinstall the app.";
    }
    loaded_id = core.id;
    REXLOG_INFO("Launcher: loaded {}", path.string());
  }

  auto entry = library.GetSymbol<CoreEntryFn>(kCoreEntrySymbol);
  const CoreExports* exports = entry ? entry() : nullptr;
  if (!exports || exports->abi_version != kCoreAbiVersion || !exports->id ||
      std::strcmp(exports->id, core.id) != 0 || !exports->image_info) {
    REXLOG_ERROR("Launcher: {} is not a matching core library", core.library);
    return std::string(core.name) + " doesn't match this version of the app. Reinstall the app.";
  }
  return exports;
}

}  // namespace mc360::launcher
