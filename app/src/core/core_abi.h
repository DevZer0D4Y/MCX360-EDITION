// The one entry point a core library exports to the launcher.
//
// A core is the recompiled code of one title update, built as its own library
// (libmc360core_<id>.dylib in the app's Frameworks folder). Each one defines
// the same ~50k sub_XXXXXXXX symbols, so they cannot share a binary; the
// launcher loads the chosen one at startup and hands its image table to the
// runtime. Everything else stays inside the library.

#pragma once

#include <cstdint>

#include <rex/image_info.h>

namespace mc360 {

// Bump when CoreExports changes. A core built against another version is
// refused instead of being called through a mismatched struct.
inline constexpr uint32_t kCoreAbiVersion = 1;

struct CoreExports {
  uint32_t abi_version;
  // The core's folder name under app/cores/, e.g. "tu9".
  const char* id;
  // The generated PPCImageConfig: code/image layout and the function table.
  const rex::PPCImageInfo* image_info;
};

using CoreEntryFn = const CoreExports* (*)();
inline constexpr const char* kCoreEntrySymbol = "mc360_core_exports";

}  // namespace mc360
