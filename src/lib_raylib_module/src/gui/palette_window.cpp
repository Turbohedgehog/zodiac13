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

#include "palette_window.h"

#include <algorithm>
#include <format>
#include <memory>
#include <string>

#include <imgui.h>

#include <z13/components/station.h>
#include <z13_primitives/palette.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

// Matches the SELECT_SLOT_1..9 actions' default keys.
constexpr size_t kNumberedSlots = 9;

}  // namespace

PaletteWindow::PaletteWindow(flecs::world world) : Window(world, "Block palette") {}

void PaletteWindow::DrawBody() {
  const auto* palette = World().try_get<z13::building::primitives::BlockPalette>();
  if (palette == nullptr) {
    DrawError("No block palette loaded");
    return;
  }
  const auto& primitives = palette->palette.primitives;
  for (size_t slot = 0; slot < std::min(primitives.size(), z13::station::kPaletteWindowSlots); ++slot) {
    const auto& primitive = primitives[slot];
    const std::string label = slot < kNumberedSlots ? std::format("{}  {}", slot + 1, primitive.name) : primitive.name;
    if (ImGui::Button(label.c_str(), kButtonSize)) {
      World().set(z13::station::PaletteChoice {.slot = slot});
      RequestCloseMenu();
    }
  }
}

WindowPtr MakePaletteWindow(flecs::world world) {
  return std::make_shared<PaletteWindow>(world);
}

}  // namespace z13::raylib::gui
