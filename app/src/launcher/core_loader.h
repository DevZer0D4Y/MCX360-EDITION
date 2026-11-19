// Loads a core library from the app bundle and returns its image table.

#pragma once

#include <string>
#include <variant>

#include "core/core_abi.h"
#include "launcher/launcher.h"

namespace mc360::launcher {

// The loaded core's exports, or an error to show the player. A core stays
// loaded for the rest of the process: its code is what the runtime runs, and
// switching to another title update means restarting the app.
std::variant<const CoreExports*, std::string> LoadCore(const CoreInfo& core);

}  // namespace mc360::launcher
