// Reads the identifying fields of an XEX2 file (default.xex) or title update
// patch (default.xexp) from its plaintext header. Nothing is decrypted or
// decompressed: the optional headers that carry these fields are never
// encrypted, only the image after them.
//
// cmake/mc360_cores.cmake reads the same fields from the build's own files with
// the same layout, so a core's expectations and what the launcher finds on the
// phone come from one definition.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace mc360::launcher {

struct XexInfo {
  uint32_t module_flags = 0;

  // From the execution info header (0x00040006).
  bool has_execution_info = false;
  uint32_t media_id = 0;
  uint32_t version = 0;       // version of this file (the TU's, for a patch)
  uint32_t base_version = 0;  // version of the original disc release
  uint32_t title_id = 0;

  // From the delta patch descriptor (0x000005FF), title updates only.
  bool has_delta_patch = false;
  uint32_t patch_target_version = 0;
  uint32_t patch_source_version = 0;

  bool is_patch() const { return (module_flags & 0x70u) != 0; }  // MODULE_PATCH|FULL|DELTA

  // The game version once this file is in use: a patch's target version, or
  // the file's own version.
  std::optional<uint32_t> resulting_version() const {
    if (has_delta_patch) {
      return patch_target_version;
    }
    if (has_execution_info) {
      return version;
    }
    return std::nullopt;
  }
};

// nullopt when the file is missing, unreadable, or not an XEX2 file.
std::optional<XexInfo> ReadXexInfo(const std::filesystem::path& path);

// Parses an in-memory header (exposed for tests).
std::optional<XexInfo> ParseXexInfo(const uint8_t* data, size_t size);

// "major.minor.build.qfe", the way the console shows title versions.
std::string FormatXexVersion(uint32_t version);

}  // namespace mc360::launcher
