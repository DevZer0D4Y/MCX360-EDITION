#include "launcher/stfs_archive.h"

#include <span>
#include <vector>

#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>

namespace mc360::launcher {

namespace {

namespace rfs = rex::filesystem;
using rex::X_STATUS;

class StfsArchive final : public Archive {
 public:
  explicit StfsArchive(std::unique_ptr<rfs::StfsContainerDevice> device)
      : device_(std::move(device)) {
    // Walk the package tree; keep each file's entry for reading later.
    std::vector<rfs::Entry*> pending{device_->ResolvePath("/")};
    while (!pending.empty()) {
      rfs::Entry* entry = pending.back();
      pending.pop_back();
      if (!entry) {
        continue;
      }
      for (const auto& child : entry->children()) {
        pending.push_back(child.get());
      }
      if (entry->attributes() & rfs::kFileAttributeDirectory) {
        continue;
      }
      std::string path = entry->path();
      if (!NormalizeArchivePath(path)) {
        continue;
      }
      files_.push_back({std::move(path), entry->size(), entries_.size()});
      entries_.push_back(entry);
    }
  }

  const std::vector<ArchiveFile>& files() const override { return files_; }

  bool Read(const ArchiveFile& file, uint64_t offset, std::span<uint8_t> out) override {
    if (file.cookie >= entries_.size()) {
      return false;
    }
    rfs::File* handle = nullptr;
    if (entries_[file.cookie]->Open(rfs::FileAccess::kFileReadData, &handle) !=
            X_STATUS_SUCCESS ||
        !handle) {
      return false;
    }
    // STFS files are not contiguous (hash blocks are interleaved), so read
    // through the device rather than mapping.
    size_t bytes_read = 0;
    const auto status = handle->ReadSync(out, size_t(offset), &bytes_read);
    handle->Destroy();
    return status == X_STATUS_SUCCESS && bytes_read == out.size();
  }

 private:
  std::unique_ptr<rfs::StfsContainerDevice> device_;
  std::vector<ArchiveFile> files_;
  std::vector<rfs::Entry*> entries_;
};

}  // namespace

std::unique_ptr<Archive> OpenStfsArchive(const std::filesystem::path& path, std::string* error) {
  auto device = std::make_unique<rfs::StfsContainerDevice>("", path);
  if (!device->Initialize()) {
    if (error) {
      *error = "This title update package couldn't be read. It may be damaged or incomplete.";
    }
    return nullptr;
  }
  return std::make_unique<StfsArchive>(std::move(device));
}

}  // namespace mc360::launcher
