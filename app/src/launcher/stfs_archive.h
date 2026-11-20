// Title update packages (STFS: CON/LIVE/PIRS), read with the SDK's
// StfsContainerDevice, the same reader `rexglue dump` uses.

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "launcher/archive.h"

namespace mc360::launcher {

std::unique_ptr<Archive> OpenStfsArchive(const std::filesystem::path& path, std::string* error);

}  // namespace mc360::launcher
