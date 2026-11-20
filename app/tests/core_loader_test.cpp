// Loading core libraries the way the launcher does: found next to the
// executable, checked against the core table, refused when wrong.
// Built only with REXGLUE_SDK (see CMakeLists.txt).

#include <cstdio>
#include <string>
#include <variant>

#include "launcher/core_loader.h"

using namespace mc360::launcher;

namespace {

int failures = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
      ++failures;                                                   \
    }                                                               \
  } while (0)

const CoreInfo Core(const char* id, const char* library) {
  return {id, id, library, true, 0, 0, 0, true, "tu", "userdata", "cache"};
}

bool Loaded(const std::variant<const mc360::CoreExports*, std::string>& result, const char* id) {
  const auto* exports = std::get_if<const mc360::CoreExports*>(&result);
  if (!exports) {
    std::printf("  error: %s\n", std::get<std::string>(result).c_str());
    return false;
  }
  return std::string((*exports)->id) == id && (*exports)->image_info->code_base == 0x82100000;
}

}  // namespace

int main() {
  const CoreInfo missing = Core("tu20", "libmc360core_missing" CORE_SUFFIX);
  const CoreInfo mislabelled = Core("tu14", "libmc360core_tu9" CORE_SUFFIX);  // holds tu9
  const CoreInfo tu9 = Core("tu9", "libmc360core_tu9" CORE_SUFFIX);
  const CoreInfo tu14 = Core("tu14", "libmc360core_tu14" CORE_SUFFIX);

  CHECK(!Loaded(LoadCore(missing), "tu20"));
  // A library that isn't the core it should be is refused and unloaded...
  CHECK(!Loaded(LoadCore(mislabelled), "tu14"));
  // ...so the right one can still be loaded without restarting.
  CHECK(Loaded(LoadCore(tu9), "tu9"));
  CHECK(Loaded(LoadCore(tu9), "tu9"));
  // One core per process: another one needs a restart.
  CHECK(!Loaded(LoadCore(tu14), "tu14"));

  std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
  return failures != 0;
}
