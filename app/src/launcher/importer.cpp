#include "launcher/importer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <optional>
#include <system_error>

#include "launcher/xdvdfs.h"
#include "launcher/xex_info.h"

namespace mc360::launcher {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kContentTypeInstaller = 0x000B0000;  // title updates
constexpr const char* kTempFolder = ".import-tmp";
constexpr uint64_t kSpaceMargin = 64ull << 20;

uint32_t LoadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

std::string Hex32(uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%08X", value);
  return buffer;
}

bool IEquals(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
           return std::tolower(x) == std::tolower(y);
         });
}

bool IsEmptyDirectory(const fs::path& path) {
  std::error_code ec;
  return fs::is_directory(path, ec) && fs::directory_iterator(path, ec) == fs::directory_iterator();
}

// UTF-16BE (the package's display name) to UTF-8, stopping at the first NUL.
std::string Utf16BeToUtf8(const uint8_t* data, size_t bytes) {
  std::string out;
  for (size_t i = 0; i + 1 < bytes; i += 2) {
    const uint32_t c = (uint32_t(data[i]) << 8) | data[i + 1];
    if (c == 0) {
      break;
    }
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0xD800 || c > 0xDFFF) {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

// Package header fields (STFS: CON/LIVE/PIRS), at their fixed offsets.
struct PackageHeader {
  uint32_t content_type;
  uint32_t version;
  uint32_t title_id;
  std::string display_name;
};

std::optional<PackageHeader> ReadPackageHeader(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::array<uint8_t, 0x491> header{};
  if (!in.read(reinterpret_cast<char*>(header.data()), header.size())) {
    return std::nullopt;
  }
  const uint32_t magic = LoadBE32(header.data());
  if (magic != 0x434F4E20 && magic != 0x4C495645 && magic != 0x50495253) {  // CON LIVE PIRS
    return std::nullopt;
  }
  PackageHeader result;
  result.content_type = LoadBE32(header.data() + 0x344);
  result.version = LoadBE32(header.data() + 0x358);   // execution info: version
  result.title_id = LoadBE32(header.data() + 0x360);  // execution info: title ID
  result.display_name = Utf16BeToUtf8(header.data() + 0x411, 0x80);
  return result;
}

std::optional<uint32_t> KnownTitleId(std::span<const CoreInfo> cores) {
  for (const CoreInfo& core : cores) {
    if (core.identity_known) {
      return core.title_id;
    }
  }
  return std::nullopt;
}

// A top-level folder whose default.xexp is this version, if any.
std::optional<fs::path> FolderWithTitleUpdate(const fs::path& base_dir, uint32_t version) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(base_dir, ec)) {
    std::error_code entry_ec;
    if (!entry.is_directory(entry_ec) || entry.path().filename() == kTempFolder) {
      continue;
    }
    const auto info = ReadXexInfo(entry.path() / "default.xexp");
    if (info && info->resulting_version() == version) {
      return entry.path();
    }
  }
  return std::nullopt;
}

std::string FolderName(const fs::path& path) {
  return path.filename().string() + "/";
}

const CoreInfo* CoreForVersion(std::span<const CoreInfo> cores, uint32_t version) {
  for (const CoreInfo& core : cores) {
    if (core.identity_known && core.has_update && core.game_version == version) {
      return &core;
    }
  }
  return nullptr;
}

}  // namespace

std::string FormatBytes(uint64_t bytes) {
  char buffer[32];
  if (bytes >= (1ull << 30)) {
    std::snprintf(buffer, sizeof(buffer), "%.1f GB", double(bytes) / double(1ull << 30));
  } else if (bytes >= (1ull << 20)) {
    std::snprintf(buffer, sizeof(buffer), "%.0f MB", double(bytes) / double(1ull << 20));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.0f KB", double(bytes) / 1024.0);
  }
  return buffer;
}

std::vector<ImportCandidate> FindImportCandidates(const fs::path& base_dir,
                                                  std::span<const CoreInfo> cores) {
  std::vector<ImportCandidate> candidates;
  const auto known_title = KnownTitleId(cores);
  for (const fs::path& dir : {base_dir, base_dir / "import"}) {
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      std::error_code entry_ec;
      if (entry.is_regular_file(entry_ec) && entry.path().filename().string()[0] != '.') {
        files.push_back(entry.path());
      }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& path : files) {
      std::error_code size_ec;
      const uint64_t size = fs::file_size(path, size_ec);
      if (auto header = ReadPackageHeader(path)) {
        if (header->content_type != kContentTypeInstaller) {
          continue;  // a save, DLC or something else, not a title update
        }
        ImportCandidate c;
        c.kind = ImportCandidate::Kind::kTitleUpdate;
        c.path = path;
        c.file_size = size;
        c.title_id = header->title_id;
        c.version = header->version;
        c.display_name = header->display_name;
        if (known_title && c.title_id != *known_title) {
          c.state = ImportCandidate::State::kBlocked;
          c.note = "This title update is for another game (Title ID " + Hex32(c.title_id) + ").";
        } else if (auto folder = c.version ? FolderWithTitleUpdate(base_dir, c.version)
                                           : std::nullopt) {
          c.state = ImportCandidate::State::kInstalled;
          c.note = "Already unpacked in " + FolderName(*folder) + ". You can delete this file.";
        }
        c.core = c.version ? CoreForVersion(cores, c.version) : nullptr;
        candidates.push_back(std::move(c));
      } else if (XdvdfsArchive::LooksLikeDiscImage(path)) {
        ImportCandidate c;
        c.kind = ImportCandidate::Kind::kDiscImage;
        c.path = path;
        c.file_size = size;
        const fs::path game = base_dir / "game";
        std::error_code exists_ec;
        if (fs::is_regular_file(game / "default.xex", exists_ec)) {
          c.state = ImportCandidate::State::kInstalled;
          c.note = "game/ already has disc files. You can delete this file.";
        } else if (fs::exists(game, exists_ec) && !IsEmptyDirectory(game)) {
          c.state = ImportCandidate::State::kBlocked;
          c.note = "game/ already has other files in it. Empty it first.";
        }
        candidates.push_back(std::move(c));
      }
    }
  }
  // What still needs doing first; finished ones last.
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const ImportCandidate& a, const ImportCandidate& b) {
                     auto rank = [](ImportCandidate::State s) {
                       return s == ImportCandidate::State::kReady     ? 0
                              : s == ImportCandidate::State::kBlocked ? 1
                                                                      : 2;
                     };
                     return rank(a.state) < rank(b.state);
                   });
  return candidates;
}

bool HasPendingImports(const std::vector<ImportCandidate>& candidates) {
  return std::any_of(candidates.begin(), candidates.end(), [](const ImportCandidate& c) {
    return c.state == ImportCandidate::State::kReady;
  });
}

ImportJob::ImportJob(ImportCandidate candidate, fs::path base_dir, std::span<const CoreInfo> cores,
                     ArchiveOpener opener)
    : candidate_(std::move(candidate)),
      base_dir_(std::move(base_dir)),
      cores_(cores),
      opener_(std::move(opener)) {}

ImportJob::~ImportJob() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ImportJob::Start() {
  thread_ = std::thread([this] { Run(); });
}

std::string ImportJob::current_file() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_file_;
}

std::string ImportJob::message() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return message_;
}

bool ImportJob::Fail(std::string why) {
  std::error_code ec;
  fs::remove_all(base_dir_ / kTempFolder, ec);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    message_ = std::move(why);
  }
  succeeded_ = false;
  finished_ = true;
  return false;
}

fs::path ImportJob::ChooseTitleUpdateFolder(uint32_t version, std::string* why) const {
  if (auto folder = FolderWithTitleUpdate(base_dir_, version)) {
    *why = "This title update is already unpacked in " + FolderName(*folder) + ".";
    return {};
  }
  const std::string version_text = FormatXexVersion(version);
  std::string name;
  if (const CoreInfo* core = CoreForVersion(cores_, version)) {
    // The folder the core looks in first, unless something else is there.
    const fs::path declared = base_dir_ / core->update_dir;
    std::error_code ec;
    if (!fs::exists(declared, ec) || IsEmptyDirectory(declared)) {
      return declared;
    }
    name = std::string(core->update_dir) + "-" + version_text;
  } else {
    name = "tu-" + version_text;
  }
  fs::path folder = base_dir_ / name;
  std::error_code ec;
  for (int n = 2; fs::exists(folder, ec) && !IsEmptyDirectory(folder); ++n) {
    folder = base_dir_ / (name + "-" + std::to_string(n));
  }
  return folder;
}

void ImportJob::Run() {
  const bool is_disc = candidate_.kind == ImportCandidate::Kind::kDiscImage;
  const fs::path temp = base_dir_ / kTempFolder;
  std::error_code ec;
  fs::remove_all(temp, ec);  // left over from an import that was cut short

  std::string open_error;
  std::unique_ptr<Archive> archive = opener_(candidate_, &open_error);
  if (!archive) {
    Fail(open_error.empty() ? "The file couldn't be read." : open_error);
    return;
  }

  // What to unpack. A disc's $SystemUpdate folder is a console dashboard
  // update the game never reads.
  std::vector<const ArchiveFile*> files;
  uint64_t total = 0;
  const ArchiveFile* key_file = nullptr;
  const char* key_name = is_disc ? "default.xex" : "default.xexp";
  for (const ArchiveFile& file : archive->files()) {
    const std::string_view path = file.path;
    const auto slash = path.find('/');
    if (is_disc && slash != std::string_view::npos &&
        IEquals(path.substr(0, slash), "$SystemUpdate")) {
      continue;
    }
    if (IEquals(path, key_name)) {
      key_file = &file;
    }
    files.push_back(&file);
    total += file.size;
  }
  if (!key_file) {
    Fail(is_disc ? "This disc image has no default.xex." : "This package has no default.xexp.");
    return;
  }

  // Identify it before writing anything.
  std::vector<uint8_t> header(size_t(std::min<uint64_t>(key_file->size, 1u << 20)));
  if (!archive->Read(*key_file, 0, header)) {
    Fail(std::string("Couldn't read ") + key_name + " from the file.");
    return;
  }
  const auto info = ParseXexInfo(header.data(), header.size());
  const auto known_title = KnownTitleId(cores_);
  if (!info || !info->has_execution_info) {
    Fail(std::string(key_name) + " in this file isn't readable.");
    return;
  }
  if (known_title && info->title_id != *known_title) {
    Fail("This is a different game (Title ID " + Hex32(info->title_id) + ").");
    return;
  }

  fs::path destination;
  uint32_t version = 0;
  if (is_disc) {
    destination = base_dir_ / "game";
    if (fs::exists(destination, ec) && !IsEmptyDirectory(destination)) {
      Fail("game/ already has files in it. Empty it first.");
      return;
    }
  } else {
    version = info->resulting_version().value_or(0);
    std::string why;
    destination = ChooseTitleUpdateFolder(version, &why);
    if (destination.empty()) {
      Fail(why);
      return;
    }
  }

  const auto space = fs::space(base_dir_, ec);
  if (!ec && space.available < total + kSpaceMargin) {
    Fail("Not enough free space: this needs " + FormatBytes(total + kSpaceMargin) + " and " +
         FormatBytes(space.available) + " is free.");
    return;
  }

  bytes_total_ = total;
  fs::create_directories(temp, ec);
  std::vector<uint8_t> buffer(4u << 20);
  for (const ArchiveFile* file : files) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      current_file_ = file->path;
    }
    const fs::path out_path = temp / fs::path(file->path);
    fs::create_directories(out_path.parent_path(), ec);
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      Fail("Couldn't write " + file->path + ".");
      return;
    }
    for (uint64_t offset = 0; offset < file->size;) {
      const size_t chunk = size_t(std::min<uint64_t>(buffer.size(), file->size - offset));
      if (!archive->Read(*file, offset, std::span<uint8_t>(buffer.data(), chunk))) {
        Fail("Couldn't read " + file->path + ". The file may be damaged or incomplete.");
        return;
      }
      if (!out.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(chunk))) {
        Fail("Couldn't write " + file->path + ". Is the phone out of space?");
        return;
      }
      offset += chunk;
      bytes_done_ += chunk;
    }
  }
  archive.reset();

  // Into place in one step, so a cut-short import never leaves half a folder.
  if (IsEmptyDirectory(destination)) {
    fs::remove(destination, ec);
  }
  fs::rename(temp, destination, ec);
  if (ec) {
    Fail("Couldn't move the files into " + FolderName(destination) + ".");
    return;
  }

  std::string message;
  const std::string source = candidate_.path.filename().string();
  if (is_disc) {
    message = "Disc files unpacked into game/.";
  } else {
    message = "Title update " + FormatXexVersion(version) + " unpacked into " +
              FolderName(destination) + ".";
    if (!CoreForVersion(cores_, version)) {
      message += " This copy of the app can't play that title update yet.";
    }
  }
  message += " You can delete " + source + " now.";
  {
    std::lock_guard<std::mutex> lock(mutex_);
    message_ = std::move(message);
    current_file_.clear();
  }
  succeeded_ = true;
  finished_ = true;
}

}  // namespace mc360::launcher
