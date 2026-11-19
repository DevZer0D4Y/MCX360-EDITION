#include "launcher/gamepad_poll.h"

#import <GameController/GameController.h>

#include <cmath>

#include <imgui.h>

namespace mc360::launcher {

namespace {

GCExtendedGamepad* CurrentGamepad() {
  GCController* controller = GCController.current;
  if (!controller.extendedGamepad) {
    for (GCController* candidate in GCController.controllers) {
      if (candidate.extendedGamepad) {
        controller = candidate;
        break;
      }
    }
  }
  return controller.extendedGamepad;
}

}  // namespace

void PollLauncherGamepad(ImGuiIO& io) {
  GCExtendedGamepad* pad = CurrentGamepad();
  auto button = [&io](ImGuiKey key, bool pressed) { io.AddKeyEvent(key, pressed); };
  // A activates and B goes back, as in the console's own menus.
  button(ImGuiKey_GamepadFaceDown, pad && pad.buttonA.pressed);
  button(ImGuiKey_GamepadFaceRight, pad && pad.buttonB.pressed);
  button(ImGuiKey_GamepadFaceLeft, pad && pad.buttonX.pressed);
  button(ImGuiKey_GamepadFaceUp, pad && pad.buttonY.pressed);
  button(ImGuiKey_GamepadDpadUp, pad && pad.dpad.up.pressed);
  button(ImGuiKey_GamepadDpadDown, pad && pad.dpad.down.pressed);
  button(ImGuiKey_GamepadDpadLeft, pad && pad.dpad.left.pressed);
  button(ImGuiKey_GamepadDpadRight, pad && pad.dpad.right.pressed);
  button(ImGuiKey_GamepadL1, pad && pad.leftShoulder.pressed);
  button(ImGuiKey_GamepadR1, pad && pad.rightShoulder.pressed);
  button(ImGuiKey_GamepadStart, pad && pad.buttonMenu.pressed);

  // Left stick moves the selection too. Same dead zone as XInput's.
  constexpr float kDeadZone = 7849.0f / 32767.0f;
  auto axis = [&io](float value, ImGuiKey negative_key, ImGuiKey positive_key) {
    const float magnitude = std::fabs(value);
    const bool active = magnitude > kDeadZone;
    const float scaled = active ? (magnitude - kDeadZone) / (1.0f - kDeadZone) : 0.0f;
    io.AddKeyAnalogEvent(value < 0.0f ? negative_key : positive_key, active, scaled);
    io.AddKeyAnalogEvent(value < 0.0f ? positive_key : negative_key, false, 0.0f);
  };
  const float x = pad ? pad.leftThumbstick.xAxis.value : 0.0f;
  const float y = pad ? pad.leftThumbstick.yAxis.value : 0.0f;
  axis(x, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight);
  axis(y, ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp);
}

}  // namespace mc360::launcher
