// Setting the game up on the phone alone: the player drops their disc image
// (.iso) and title update package into the app's folder in the Files app, and
// the launcher menu offers to unpack them into game/ and the title update's
// folder. This replaces extract-xiso and Velocity on a computer.
//
// Only files are moved around here; nothing is recompiled. A title update can
// be played once this app includes its core (see cores/README.md).

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "launcher/archive.h"
#include "launcher/launcher.h"

namespace mc360::launcher {

struct ImportCandidate {
  enum class Kind { kDiscImage, kTitleUpdate };
  enum class State {
    kReady,      // can be imported
    kInstalled,  // its files are already in place
    kBlocked,    // can't be imported as things are; `note` says why
  };

  Kind kind = Kind::kDiscImage;
  State state = State::kReady;
  std::filesystem::path path;
  uint64_t file_size = 0;
  // Title updates: from the package header.
  uint32_t title_id = 0;
  uint32_t version = 0;  // 0 when the header doesn't say
  std::string display_name;
  // The core this title update belongs to, if this app has it.
  const CoreInfo* core = nullptr;
  std::string note;
};

// Disc images and title update packages in `base_dir` and `base_dir`/import/.
std::vector<ImportCandidate> FindImportCandidates(const std::filesystem::path& base_dir,
                                                  std::span<const CoreInfo> cores);
bool HasPendingImports(const std::vector<ImportCandidate>& candidates);

// Opens a candidate's contents. Title update packages need the SDK's STFS
// reader, so the app supplies this (see stfs_archive.h).
using ArchiveOpener =
    std::function<std::unique_ptr<Archive>(const ImportCandidate&, std::string* error)>;

// Unpacks one candidate on a background thread. The UI polls progress.
class ImportJob {
 public:
  ImportJob(ImportCandidate candidate, std::filesystem::path base_dir,
            std::span<const CoreInfo> cores, ArchiveOpener opener);
  ~ImportJob();  // waits for the thread

  void Start();
  void RunForTest() { Run(); }

  bool finished() const { return finished_.load(); }
  bool succeeded() const { return succeeded_.load(); }
  uint64_t bytes_done() const { return bytes_done_.load(); }
  uint64_t bytes_total() const { return bytes_total_.load(); }
  std::string current_file() const;
  // After finished(): what happened, for the player.
  std::string message() const;
  const ImportCandidate& candidate() const { return candidate_; }

 private:
  void Run();
  bool Fail(std::string why);
  std::filesystem::path ChooseTitleUpdateFolder(uint32_t version, std::string* why) const;

  ImportCandidate candidate_;
  std::filesystem::path base_dir_;
  std::span<const CoreInfo> cores_;
  ArchiveOpener opener_;
  std::thread thread_;
  std::atomic<bool> finished_{false};
  std::atomic<bool> succeeded_{false};
  std::atomic<uint64_t> bytes_done_{0};
  std::atomic<uint64_t> bytes_total_{0};
  mutable std::mutex mutex_;
  std::string current_file_;
  std::string message_;
};

// "1.2 GB", "340 MB", ...
std::string FormatBytes(uint64_t bytes);

}  // namespace mc360::launcher
