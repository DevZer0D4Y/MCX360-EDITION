// Feeds a controller into ImGui's gamepad navigation while the launcher menu is
// up. The runtime's input system does not exist yet at that point, and SDL's
// gamepad subsystem is left alone on purpose: the game's SDL input driver
// learns about connected pads from the "added" events SDL sends when that
// subsystem starts, so starting it early would hide them from the game.
// On iOS this reads the GameController framework directly instead.

#pragma once

struct ImGuiIO;

namespace mc360::launcher {

void PollLauncherGamepad(ImGuiIO& io);

}  // namespace mc360::launcher
