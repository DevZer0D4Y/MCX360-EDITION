// Compiled into every core library; MC360_CORE_ID is set per core by
// cmake/mc360_cores.cmake.

#include "mc360_init.h"

#include "core/core_abi.h"

#ifndef MC360_CORE_ID
#error "MC360_CORE_ID must be defined by the build"
#endif

#if defined(_WIN32)
#define MC360_CORE_EXPORT __declspec(dllexport)
#else
#define MC360_CORE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" MC360_CORE_EXPORT const mc360::CoreExports* mc360_core_exports() {
  static const mc360::CoreExports exports{
      mc360::kCoreAbiVersion,
      MC360_CORE_ID,
      &PPCImageConfig,
  };
  return &exports;
}
