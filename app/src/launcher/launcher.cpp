#include "launcher/launcher.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <system_error>

#include "launcher/xex_info.h"

namespace mc360::launcher {

namespace fs = std::filesystem;

namespace {

std::string Hex32(uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%08X", value);
  return buffer;
}

bool IsDirectory(const fs::path& path) {
  std::error_code ec;
  return fs::is_directory(path, ec);
}

bool IsFile(const fs::path& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

// Whether a default.xexp is the title update `core` was built from.
bool PatchMatches(const XexInfo& patch, const CoreInfo& core) {
  const auto version = patch.resulting_version();
  if (!version || *version != core.game_version) {
    return false;
  }
  return !patch.has_execution_info || patch.title_id == core.title_id;
}

std::string DescribePatch(const std::optional<XexInfo>& patch) {
  if (!patch) {
    return "a file that isn't a title update";
  }
  if (auto version = patch->resulting_version()) {
    return "version " + FormatXexVersion(*version);
  }
  return "a title update of unknown version";
}

// "userdata-<n>" for n >= 2; the number, or 0 if `name` isn't one.
int InstanceNumberFromName(const std::string& name, const std::string& save_dir) {
  const std::string prefix = save_dir + "-";
  if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) {
    return 0;
  }
  const std::string digits = name.substr(prefix.size());
  if (digits.size() > 4 ||
      !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); })) {
    return 0;
  }
  const int number = std::stoi(digits);
  return number >= 2 ? number : 0;
}

std::string Trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

}  // namespace

bool CoreStatus::has_error() const {
  return std::any_of(issues.begin(), issues.end(),
                     [](const Issue& i) { return i.severity == Issue::Severity::kError; });
}

bool CoreStatus::has_warning() const {
  return std::any_of(issues.begin(), issues.end(),
                     [](const Issue& i) { return i.severity == Issue::Severity::kWarning; });
}

Launcher::Launcher(fs::path base_dir, std::span<const CoreInfo> cores)
    : base_dir_(std::move(base_dir)), core_infos_(cores) {}

fs::path Launcher::InstanceFolder(const fs::path& base_dir, const CoreInfo& core, int instance) {
  if (instance <= 1) {
    return base_dir / core.save_dir;
  }
  return base_dir / (std::string(core.save_dir) + "-" + std::to_string(instance));
}

void Launcher::Scan() {
  statuses_.clear();
  statuses_.reserve(core_infos_.size());
  for (const CoreInfo& core : core_infos_) {
    statuses_.push_back(ScanCore(core));
  }
}

CoreStatus Launcher::ScanCore(const CoreInfo& core) const {
  CoreStatus status;
  status.core = &core;
  auto warn = [&status](std::string text) {
    status.issues.push_back({Issue::Severity::kWarning, std::move(text)});
  };

  // --- The disc files, shared by every core.
  const fs::path xex_path = game_dir() / "default.xex";
  if (!IsFile(xex_path)) {
    status.issues.push_back(
        {Issue::Severity::kError,
         "game/default.xex not found. Copy the files from your disc into the game folder, or "
         "put your disc image (.iso) in this app's folder to unpack it here."});
  } else if (core.identity_known) {
    const auto xex = ReadXexInfo(xex_path);
    if (!xex || !xex->has_execution_info) {
      warn("game/default.xex couldn't be read as an Xbox 360 game.");
    } else if (xex->title_id != core.title_id) {
      warn("game/default.xex is a different game (Title ID " + Hex32(xex->title_id) +
           ", this needs " + Hex32(core.title_id) + ").");
    } else if (xex->media_id != core.media_id) {
      warn("game/default.xex is from a different disc version (Media ID " + Hex32(xex->media_id) +
           ", this needs " + Hex32(core.media_id) + ").");
    }
  }

  // The runtime applies a default.xexp sitting next to default.xex by itself,
  // whichever core is running, so one from another title update would be
  // patched over this core's code.
  if (core.identity_known) {
    const fs::path stray = game_dir() / "default.xexp";
    if (IsFile(stray)) {
      const auto patch = ReadXexInfo(stray);
      const bool ok = core.has_update && patch && PatchMatches(*patch, core);
      if (!ok) {
        warn("game/default.xexp is " + DescribePatch(patch) + ", not " + core.name + " (" +
             FormatXexVersion(core.game_version) +
             "). Move it out of the game folder into its own title update folder.");
      }
    }
  }

  // --- The title update.
  if (!update_root_override_.empty()) {
    status.update_root = update_root_override_;
  } else if (core.has_update) {
    const fs::path declared = base_dir_ / core.update_dir;
    const std::string declared_name = std::string(core.update_dir) + "/";
    if (!core.identity_known) {
      // Nothing to compare against: mount the folder as the single-build app did.
      if (IsDirectory(declared)) {
        status.update_root = declared;
      }
    } else {
      auto matches = [&core](const fs::path& dir) {
        const auto patch = ReadXexInfo(dir / "default.xexp");
        return patch && PatchMatches(*patch, core);
      };
      if (IsDirectory(declared) && matches(declared)) {
        status.update_root = declared;
      } else {
        // Accept the right title update in any folder, so files dropped into
        // a folder with another name are still found.
        std::vector<fs::path> candidates;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(base_dir_, ec)) {
          std::error_code entry_ec;
          if (entry.is_directory(entry_ec) && entry.path() != declared &&
              entry.path().filename() != "game") {
            candidates.push_back(entry.path());
          }
        }
        std::sort(candidates.begin(), candidates.end());
        for (const auto& candidate : candidates) {
          if (matches(candidate)) {
            status.update_root = candidate;
            break;
          }
        }
        if (status.update_root.empty()) {
          if (IsDirectory(declared)) {
            // Starting anyway mounts it, as the single-build app did.
            status.update_root = declared;
            const fs::path patch_path = declared / "default.xexp";
            if (!IsFile(patch_path)) {
              warn(declared_name + "default.xexp not found. Put " + core.name +
                   "'s files in the " + declared_name + " folder.");
            } else {
              warn(declared_name + " holds " + DescribePatch(ReadXexInfo(patch_path)) +
                   ", not " + core.name + " (" + FormatXexVersion(core.game_version) + ").");
            }
          } else {
            warn(std::string(core.name) + " files not found. Put its title update package in "
                 "this app's folder to unpack it here, or its default.xexp and res folder in a "
                 "folder named " + declared_name + ". Without them, textures, items and fonts "
                 "look wrong.");
          }
        }
      }
    }
  }

  // --- Instances: <save_dir> is 1, <save_dir>-<n> are the others.
  status.instances.push_back({1, InstanceFolder(base_dir_, core, 1)});
  std::vector<int> numbers;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(base_dir_, ec)) {
    std::error_code entry_ec;
    if (!entry.is_directory(entry_ec)) {
      continue;
    }
    if (int number = InstanceNumberFromName(entry.path().filename().string(), core.save_dir)) {
      numbers.push_back(number);
    }
  }
  std::sort(numbers.begin(), numbers.end());
  numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
  for (int number : numbers) {
    status.instances.push_back({number, InstanceFolder(base_dir_, core, number)});
  }
  return status;
}

std::vector<Selection> Launcher::Options() const {
  std::vector<Selection> options;
  for (const CoreStatus& status : statuses_) {
    for (const Instance& instance : status.instances) {
      options.push_back({status.core->id, instance.number});
    }
  }
  return options;
}

const CoreStatus* Launcher::FindCore(std::string_view id) const {
  for (const CoreStatus& status : statuses_) {
    if (id == status.core->id) {
      return &status;
    }
  }
  return nullptr;
}

bool Launcher::ShouldShowMenu(bool always) const {
  if (always || statuses_.size() != 1) {
    return true;
  }
  const CoreStatus& only = statuses_.front();
  return only.instances.size() != 1 || !only.issues.empty();
}

Selection Launcher::DefaultSelection() const {
  const std::vector<Selection> options = Options();
  if (auto last = LoadLastSelection()) {
    if (std::find(options.begin(), options.end(), *last) != options.end()) {
      return *last;
    }
  }
  for (const CoreStatus& status : statuses_) {
    if (status.issues.empty()) {
      return {status.core->id, status.instances.front().number};
    }
  }
  return options.empty() ? Selection{} : options.front();
}

std::optional<int> Launcher::CreateInstance(std::string_view core_id) {
  const CoreStatus* status = FindCore(core_id);
  if (!status) {
    return std::nullopt;
  }
  int next = 2;
  for (const Instance& instance : status->instances) {
    next = std::max(next, instance.number + 1);
  }
  const fs::path folder = InstanceFolder(base_dir_, *status->core, next);
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (ec) {
    return std::nullopt;
  }
  Scan();
  return next;
}

std::optional<LaunchPaths> Launcher::PathsFor(const Selection& selection) const {
  const CoreStatus* status = FindCore(selection.core_id);
  if (!status) {
    return std::nullopt;
  }
  const bool known = std::any_of(status->instances.begin(), status->instances.end(),
                                 [&](const Instance& i) { return i.number == selection.instance; });
  if (!known) {
    return std::nullopt;
  }
  LaunchPaths paths;
  paths.update_data_root = status->update_root;
  paths.user_data_root = InstanceFolder(base_dir_, *status->core, selection.instance);
  paths.cache_root = base_dir_ / status->core->cache_dir;
  return paths;
}

std::optional<Selection> Launcher::LoadLastSelection() const {
  std::ifstream in(state_path());
  if (!in) {
    return std::nullopt;
  }
  Selection selection;
  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) {
      continue;
    }
    const std::string key = Trim(line.substr(0, eq));
    std::string value = Trim(line.substr(eq + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    if (key == "last_core") {
      selection.core_id = value;
    } else if (key == "last_instance") {
      try {
        selection.instance = std::stoi(value);
      } catch (...) {
        selection.instance = 1;
      }
    }
  }
  if (selection.core_id.empty()) {
    return std::nullopt;
  }
  return selection;
}

void Launcher::SaveLastSelection(const Selection& selection) const {
  std::ofstream out(state_path(), std::ios::trunc);
  if (!out) {
    return;
  }
  out << "# The launcher's last choice; it starts on this one next time.\n"
      << "last_core = \"" << selection.core_id << "\"\n"
      << "last_instance = " << selection.instance << "\n";
}

}  // namespace mc360::launcher
