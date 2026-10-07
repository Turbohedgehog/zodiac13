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

#include "gui_windows.h"

#include <utility>

#include <imgui.h>

namespace z13::raylib::gui {

Window::Window(flecs::world world, std::string title)
    : world_(world), title_(std::move(title)) {}

Window::StackRequest Window::Draw() {
  next_ = {};

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_AlwaysAutoResize;
  if (ImGui::Begin(title_.c_str(), nullptr, flags)) {
    DrawBody();
  }
  ImGui::End();
  return next_;
}

Window::StackRequest Window::OnBack() { return {StackOp::Pop, nullptr}; }

}  // namespace z13::raylib::gui
