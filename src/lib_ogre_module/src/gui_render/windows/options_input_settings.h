#pragma once

#include "window_settings_base.h"

namespace z13::ogre::gui {

class InputSettingsWindow : public InputSettingsWindowBase {
 public:
  InputSettingsWindow(flecs::world world);

 protected:
  void DrawImpl() override;
};

}  // namespace z13::ogre::gui
