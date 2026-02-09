#pragma once

#include <string>
#include <flecs.h>

// #include "window_component.h"

namespace z13::ogre::gui {

class WindowBase {
 public:
  WindowBase(flecs::world world, std::string window_name);
  void Draw();

  const std::string& GetName() const;
  flecs::world GetWorld() const;
  virtual void OnBackEvent();

 protected:
  virtual void DrawImpl() = 0;

 private:
  std::string window_name_;
  flecs::world world_;
};

}  // namespace z13::ogre::gui