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

#include "overlays_tab.h"

#include <array>
#include <string>
#include <string_view>

#include <imgui.h>

#include <z13/components/station/overlay_layers.h>
#include <z13/components/station/rooms.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

using z13::station::RoomOverlayMode;

constexpr float kButtonWidthEm = 7.f;
constexpr float kButtonHeightEm = 2.5f;
constexpr std::string_view kOffLabel = "Off";

struct RoomMode {
  RoomOverlayMode mode;
  std::string_view label;
};

constexpr std::array kRoomModes {RoomMode {RoomOverlayMode::kOff, kOffLabel}, RoomMode {RoomOverlayMode::kSeen, "Seen"},
                                 RoomMode {RoomOverlayMode::kAll, "All"}};

// A button that stays lit while it is the choice; true on the frame it is tapped.
bool ChoiceButton(std::string_view label, bool chosen) {
  return ImGui::Selectable(std::string(label).c_str(), chosen, ImGuiSelectableFlags_None,
                           {Em(kButtonWidthEm), Em(kButtonHeightEm)});
}

void DrawRoomModes(z13::station::RoomOverlay& rooms) {
  ImGui::TextUnformatted("Rooms");
  for (const RoomMode& choice : kRoomModes) {
    if (ChoiceButton(choice.label, rooms.mode == choice.mode)) {
      rooms.mode = choice.mode;
    }
    ImGui::SameLine();
  }
  ImGui::NewLine();
}

void DrawLayers(z13::station::OverlayLayers& layers) {
  ImGui::TextUnformatted("Layers");
  if (ChoiceButton(kOffLabel, !layers.shown)) {
    layers.shown.reset();
  }
  for (const z13::station::OverlayLayer& layer : layers.layers) {
    ImGui::SameLine();
    if (ChoiceButton(layer.name, layers.IsShown(layer.name))) {
      layers.shown = layer.name;
    }
  }
}

}  // namespace

void DrawOverlaysTab(flecs::world world) {
  if (auto* rooms = world.try_get_mut<z13::station::RoomOverlay>()) {
    DrawRoomModes(*rooms);
  }
  if (auto* layers = world.try_get_mut<z13::station::OverlayLayers>()) {
    DrawLayers(*layers);
  }
}

}  // namespace z13::raylib::gui
