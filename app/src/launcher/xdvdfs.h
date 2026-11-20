// Xbox 360 disc images (XDVDFS / GDFX): full "redump" ISOs and trimmed XISOs.
//
// Reads the directory tree and file data with ordinary file reads. The SDK's
// DiscImageDevice maps the whole image into memory instead, which for a 7-8 GB
// image needs more address space than an iPhone app may have without the
// extended virtual addressing entitlement.

#pragma once

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "launcher/archive.h"

namespace mc360::launcher {

class XdvdfsArchive final : public Archive {
 public:
  // nullptr and a reason when `path` is not a readable Xbox 360 disc image.
  static std::unique_ptr<XdvdfsArchive> Open(const std::filesystem::path& path,
                                             std::string* error);
  // Cheap check (a few small reads) for the XDVDFS signature.
  static bool LooksLikeDiscImage(const std::filesystem::path& path);

  ~XdvdfsArchive() override;

  const std::vector<ArchiveFile>& files() const override { return files_; }
  bool Read(const ArchiveFile& file, uint64_t offset, std::span<uint8_t> out) override;

 private:
  XdvdfsArchive() = default;
  bool ReadAt(uint64_t offset, void* data, size_t size);
  bool ReadDirectory(uint64_t table_offset, uint32_t table_size, const std::string& prefix,
                     int depth);

  std::FILE* file_ = nullptr;
  uint64_t file_size_ = 0;
  uint64_t game_offset_ = 0;
  std::vector<ArchiveFile> files_;
};

}  // namespace mc360::launcher
