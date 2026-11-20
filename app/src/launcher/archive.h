// A read-only container of files: an Xbox 360 disc image or a content package
// (title update). The importer extracts one into the app's folders.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mc360::launcher {

struct ArchiveFile {
  std::string path;     // relative, '/'-separated, e.g. "res/terrain.png"
  uint64_t size = 0;
  uint64_t cookie = 0;  // reader-specific (a byte offset, an index, ...)
};

class Archive {
 public:
  virtual ~Archive() = default;

  // Regular files only; directories are implied by the paths.
  virtual const std::vector<ArchiveFile>& files() const = 0;

  // Reads exactly out.size() bytes of `file` starting at `offset`.
  virtual bool Read(const ArchiveFile& file, uint64_t offset, std::span<uint8_t> out) = 0;
};

// Normalises a path from a container: backslashes to '/', no leading '/'.
// Returns false for a path that would escape the destination folder.
bool NormalizeArchivePath(std::string& path);

}  // namespace mc360::launcher
