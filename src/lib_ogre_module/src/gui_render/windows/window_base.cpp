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

#include "window_base.h"

#include <imgui.h>

#include "window_component.h"

namespace z13::ogre::gui {

WindowBase::WindowBase(flecs::world world, std::string window_name)
  : window_name_(std::move(window_name))
  , world_(world) {
}

const std::string& WindowBase::GetName() const {
  return window_name_;
}

void WindowBase::Draw() {
  ImGuiIO& io = ImGui::GetIO();
  ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoSavedSettings;
                           
  if (ImGui::Begin(window_name_.c_str(), nullptr, flags)) {
    ImGui::Text("%s", window_name_.c_str());
    ImGui::Separator();

    DrawImpl();

    ImGui::End();
  }
}

flecs::world WindowBase::GetWorld() const {
  return world_;
}

void WindowBase::OnBackEvent() {
  GetWorld().ensure<z13::ogre::gui::WindowComponent>().modal_window_stack.pop();
}

}  // namespace z13::ogre::gui
