#include "launcher/xex_info.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

namespace mc360::launcher {

namespace {

constexpr uint32_t kXex2Magic = 0x58455832;  // 'XEX2'
constexpr uint32_t kHeaderExecutionInfo = 0x00040006;
constexpr uint32_t kHeaderDeltaPatchDescriptor = 0x000005FF;

// The header and every optional header it points at sit in the first
// header_size bytes; real games keep that well under this.
constexpr size_t kMaxHeaderBytes = 1u << 20;

uint32_t LoadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

}  // namespace

std::optional<XexInfo> ParseXexInfo(const uint8_t* data, size_t size) {
  if (!data || size < 0x18 || LoadBE32(data) != kXex2Magic) {
    return std::nullopt;
  }
  XexInfo info;
  info.module_flags = LoadBE32(data + 0x4);
  const uint32_t header_count = LoadBE32(data + 0x14);
  // Each optional header is a {key, value} pair after the fixed 0x18 bytes.
  if (header_count > (size - 0x18) / 8) {
    return std::nullopt;
  }
  for (uint32_t i = 0; i < header_count; ++i) {
    const uint8_t* entry = data + 0x18 + size_t(i) * 8;
    const uint32_t key = LoadBE32(entry);
    const uint32_t value = LoadBE32(entry + 4);
    if (key == kHeaderExecutionInfo) {
      // value is a file offset to a 0x18-byte struct.
      if (value > size || size - value < 0x18) {
        return std::nullopt;
      }
      const uint8_t* exec = data + value;
      info.has_execution_info = true;
      info.media_id = LoadBE32(exec + 0x0);
      info.version = LoadBE32(exec + 0x4);
      info.base_version = LoadBE32(exec + 0x8);
      info.title_id = LoadBE32(exec + 0xC);
    } else if (key == kHeaderDeltaPatchDescriptor) {
      // value is a file offset; target and source versions follow the size.
      if (value > size || size - value < 0xC) {
        return std::nullopt;
      }
      const uint8_t* delta = data + value;
      info.has_delta_patch = true;
      info.patch_target_version = LoadBE32(delta + 0x4);
      info.patch_source_version = LoadBE32(delta + 0x8);
    }
  }
  return info;
}

std::optional<XexInfo> ReadXexInfo(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }
  std::array<uint8_t, 0x18> fixed{};
  if (!file.read(reinterpret_cast<char*>(fixed.data()), fixed.size())) {
    return std::nullopt;
  }
  if (LoadBE32(fixed.data()) != kXex2Magic) {
    return std::nullopt;
  }
  // header_size covers the fixed header and all optional header data.
  size_t header_size = LoadBE32(fixed.data() + 0x8);
  if (header_size < fixed.size()) {
    return std::nullopt;
  }
  header_size = std::min(header_size, kMaxHeaderBytes);
  std::vector<uint8_t> header(header_size);
  file.clear();
  file.seekg(0);
  file.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size()));
  header.resize(size_t(file.gcount()));
  return ParseXexInfo(header.data(), header.size());
}

std::string FormatXexVersion(uint32_t version) {
  // Bit layout matches xex2_version: major:4 minor:4 build:16 qfe:8.
  const uint32_t major = (version >> 28) & 0xF;
  const uint32_t minor = (version >> 24) & 0xF;
  const uint32_t build = (version >> 8) & 0xFFFF;
  const uint32_t qfe = version & 0xFF;
  return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(build) + "." +
         std::to_string(qfe);
}

}  // namespace mc360::launcher
