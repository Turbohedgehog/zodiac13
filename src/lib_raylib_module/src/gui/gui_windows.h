/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <flecs.h>

#include <z13/components/input.h>

namespace z13::raylib::gui {

class Window;
using WindowPtr = std::shared_ptr<Window>;

// A modal window in the pause menu. Concrete windows implement DrawBody() with
// Dear ImGui widgets and set `next_` to ask the stack to pop / close / push.
class Window {
 public:
  enum class StackOp { Keep, Pop, CloseMenu };

  struct StackRequest {
    StackOp op = StackOp::Keep;
    WindowPtr push;
  };

  Window(flecs::world world, std::string title);
  virtual ~Window() = default;

  // Draws a centred, auto-sized ImGui window + body; returns the stack action.
  StackRequest Draw();

  // Invoked on the top window when the player presses Back (Esc) / a key.
  virtual StackRequest OnBack();  // default: pop
  virtual void OnKeyDown(z13::fbs::input::Keycode) {}

  const std::string& Title() const { return title_; }

 protected:
  virtual void DrawBody() = 0;

  flecs::world World() const { return world_; }
  void RequestPop() { next_.op = StackOp::Pop; }
  void RequestCloseMenu() { next_.op = StackOp::CloseMenu; }
  void RequestPush(WindowPtr window) { next_.push = std::move(window); }

  flecs::world world_;
  std::string title_;
  StackRequest next_;
};

// Pause-menu window stack (singleton). Back of the vector is the visible window.
struct WindowStack {
  std::vector<WindowPtr> windows;
};

WindowPtr MakeMainMenu(flecs::world world);
WindowPtr MakeInputSettings(flecs::world world);
WindowPtr MakeKeyBindings(flecs::world world);

}  // namespace z13::raylib::gui
