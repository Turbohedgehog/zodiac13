#pragma once

#include "window_base.h"

namespace z13::ogre::gui {

class GameplayMainMenuWindow : public WindowBase {
 public:
  GameplayMainMenuWindow(flecs::world world);

  void OnBackEvent() override;

 protected:
  void DrawImpl() override;
};

}  // namespace z13::ogre::gui
