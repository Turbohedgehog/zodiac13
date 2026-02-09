#pragma once

#include <flecs.h>

#include "window_component.h"

namespace z13::ogre::gui {

class WindowFactory {
 public:
  static WindowPtr CreateGameplayMainMenu(flecs::world world);
  static WindowPtr CreateInputSettingsMenu(flecs::world world);
  static WindowPtr CreateKeyboardBindingsMenu(flecs::world world);
};

}  // namespace z13::ogre::gui