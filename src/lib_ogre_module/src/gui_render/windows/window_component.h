#pragma once

#include <memory>
#include <stack>

namespace z13::ogre::gui {

class WindowBase;

using WindowPtr = std::shared_ptr<WindowBase>;

struct WindowComponent {
  std::stack<WindowPtr> modal_window_stack;
};

}  // namespace z13::ogre::gui