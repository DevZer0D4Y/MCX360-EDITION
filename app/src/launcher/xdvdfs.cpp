#include "launcher/xdvdfs.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

namespace mc360::launcher {

namespace {

constexpr uint64_t kSectorSize = 0x800;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kMagicSize = 20;
// Where the game partition starts in the image formats in circulation (the
// same list as the SDK's DiscImageDevice): XISO, and full XGD1/2/3 images.
constexpr uint64_t kGameOffsets[] = {0x00000000, 0x0000FB20, 0x00020600, 0x02080000, 0x0FD90000};
constexpr uint32_t kMaxTableSize = 32u << 20;
constexpr uint8_t kAttributeDirectory = 0x10;

bool Seek(std::FILE* file, uint64_t offset) {
#if defined(_WIN32)
  return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
  return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

uint16_t LoadLE16(const uint8_t* p) {
  return uint16_t(p[0] | (p[1] << 8));
}
uint32_t LoadLE32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint64_t FileSize(std::FILE* file) {
#if defined(_WIN32)
  _fseeki64(file, 0, SEEK_END);
  return uint64_t(_ftelli64(file));
#else
  fseeko(file, 0, SEEK_END);
  return uint64_t(ftello(file));
#endif
}

}  // namespace

bool NormalizeArchivePath(std::string& path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  while (!path.empty() && path.front() == '/') {
    path.erase(path.begin());
  }
  if (path.empty()) {
    return false;
  }
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string::npos) {
      end = path.size();
    }
    const std::string_view part(path.data() + start, end - start);
    if (part.empty() || part == "." || part == "..") {
      return false;
    }
    start = end + 1;
  }
  return true;
}

XdvdfsArchive::~XdvdfsArchive() {
  if (file_) {
    std::fclose(file_);
  }
}

bool XdvdfsArchive::ReadAt(uint64_t offset, void* data, size_t size) {
  if (offset > file_size_ || file_size_ - offset < size) {
    return false;
  }
  return Seek(file_, offset) && std::fread(data, 1, size, file_) == size;
}

bool XdvdfsArchive::LooksLikeDiscImage(const std::filesystem::path& path) {
  std::FILE* file = std::fopen(path.string().c_str(), "rb");
  if (!file) {
    return false;
  }
  const uint64_t size = FileSize(file);
  bool found = false;
  for (uint64_t game_offset : kGameOffsets) {
    const uint64_t at = game_offset + 32 * kSectorSize;
    char magic[kMagicSize];
    if (at + kMagicSize <= size && Seek(file, at) &&
        std::fread(magic, 1, kMagicSize, file) == kMagicSize &&
        std::memcmp(magic, kMagic, kMagicSize) == 0) {
      found = true;
      break;
    }
  }
  std::fclose(file);
  return found;
}

std::unique_ptr<XdvdfsArchive> XdvdfsArchive::Open(const std::filesystem::path& path,
                                                   std::string* error) {
  auto fail = [error](std::string why) -> std::unique_ptr<XdvdfsArchive> {
    if (error) {
      *error = std::move(why);
    }
    return nullptr;
  };
  std::unique_ptr<XdvdfsArchive> archive(new XdvdfsArchive());
  archive->file_ = std::fopen(path.string().c_str(), "rb");
  if (!archive->file_) {
    return fail("The disc image couldn't be opened.");
  }
  archive->file_size_ = FileSize(archive->file_);

  bool found = false;
  uint8_t volume[0x20];
  for (uint64_t game_offset : kGameOffsets) {
    if (archive->ReadAt(game_offset + 32 * kSectorSize, volume, sizeof(volume)) &&
        std::memcmp(volume, kMagic, kMagicSize) == 0) {
      archive->game_offset_ = game_offset;
      found = true;
      break;
    }
  }
  if (!found) {
    return fail("This isn't an Xbox 360 disc image.");
  }
  // Volume descriptor: root directory table sector and size follow the magic.
  const uint32_t root_sector = LoadLE32(volume + 20);
  const uint32_t root_size = LoadLE32(volume + 24);
  if (root_size < 14 || root_size > kMaxTableSize ||
      !archive->ReadDirectory(archive->game_offset_ + uint64_t(root_sector) * kSectorSize,
                              root_size, "", 0)) {
    return fail("The disc image is damaged or incomplete.");
  }
  std::sort(archive->files_.begin(), archive->files_.end(),
            [](const ArchiveFile& a, const ArchiveFile& b) { return a.path < b.path; });
  return archive;
}

bool XdvdfsArchive::ReadDirectory(uint64_t table_offset, uint32_t table_size,
                                  const std::string& prefix, int depth) {
  if (depth > 32 || table_size > kMaxTableSize) {
    return false;
  }
  std::vector<uint8_t> table(table_size);
  if (!ReadAt(table_offset, table.data(), table.size())) {
    return false;
  }
  // Each table is a binary tree of entries addressed by 4-byte ordinals.
  std::vector<uint16_t> pending{0};
  std::set<uint16_t> seen;
  while (!pending.empty()) {
    const uint16_t ordinal = pending.back();
    pending.pop_back();
    if (!seen.insert(ordinal).second) {
      continue;
    }
    const size_t at = size_t(ordinal) * 4;
    if (at + 14 > table.size()) {
      continue;  // 0xFFFF padding, or a bad link
    }
    const uint8_t* p = table.data() + at;
    const uint16_t left = LoadLE16(p + 0);
    const uint16_t right = LoadLE16(p + 2);
    const uint32_t sector = LoadLE32(p + 4);
    const uint32_t length = LoadLE32(p + 8);
    const uint8_t attributes = p[12];
    const uint8_t name_length = p[13];
    if (left == 0xFFFF && right == 0xFFFF) {
      continue;  // an empty directory's filler
    }
    if (left) {
      pending.push_back(left);
    }
    if (right) {
      pending.push_back(right);
    }
    if (name_length == 0 || at + 14 + name_length > table.size()) {
      continue;
    }
    std::string name(reinterpret_cast<const char*>(p + 14), name_length);
    std::string path = prefix + name;
    if (!NormalizeArchivePath(path)) {
      continue;
    }
    const uint64_t data_offset = game_offset_ + uint64_t(sector) * kSectorSize;
    if (attributes & kAttributeDirectory) {
      if (length && !ReadDirectory(data_offset, length, path + "/", depth + 1)) {
        return false;
      }
    } else {
      if (data_offset > file_size_ || file_size_ - data_offset < length) {
        return false;  // the image is cut short
      }
      files_.push_back({std::move(path), length, data_offset});
    }
  }
  return true;
}

bool XdvdfsArchive::Read(const ArchiveFile& file, uint64_t offset, std::span<uint8_t> out) {
  if (offset > file.size || file.size - offset < out.size()) {
    return false;
  }
  return ReadAt(file.cookie + offset, out.data(), out.size());
}

}  // namespace mc360::launcher
