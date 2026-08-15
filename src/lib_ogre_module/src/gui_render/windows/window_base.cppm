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

module;

#include <flecs.h>

export module z13.ogre.gui.window_base;

import std.compat;

import z13.input;

export namespace z13::ogre::gui {

class WindowBase {
 public:
  WindowBase(flecs::world world, std::string window_name);
  void Draw();

  const std::string& GetName() const;
  flecs::world GetWorld();
  virtual void OnBackEvent();
  virtual void OnKeyDownEvent(const z13::input::WindowKeyDownEvent& event);

 protected:
  virtual void DrawImpl() = 0;

 private:
  std::string window_name_;
  flecs::world world_;
};

using WindowPtr = std::shared_ptr<WindowBase>;

}  // namespace z13::ogre::gui