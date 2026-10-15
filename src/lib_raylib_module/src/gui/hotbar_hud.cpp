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

#include "hotbar_hud.h"

#include <optional>
#include <string>
#include <string_view>

#include <Eigen/Core>
#include <imgui.h>

#include <touch/touch_mapping.h>
#include <z13/components/building.h>
#include <z13/components/inventory.h>
#include <z13/components/player.h>

namespace z13::raylib::gui {

namespace {

using z13::building::ToolMode;
using z13::inventory::CarriedBy;
using z13::inventory::Hotbar;
using z13::inventory::Item;

constexpr ImU32 kCellColor = IM_COL32(0, 0, 0, 90);
constexpr ImU32 kActiveCellColor = IM_COL32(255, 255, 255, 70);
constexpr ImU32 kOutlineColor = IM_COL32(255, 255, 255, 140);
constexpr ImU32 kActiveOutlineColor = IM_COL32(255, 255, 255, 230);
constexpr ImU32 kLabelColor = IM_COL32(255, 255, 255, 220);
constexpr float kOutlineThickness = 2.f;
constexpr float kActiveOutlineThickness = 3.f;
// The cell number's inset from the cell's corner, as a part of the cell's side.
constexpr float kNumberInset = 0.06f;
// The mode's line below the crosshair, as a part of the screen's height.
constexpr float kModeLineOffset = 0.05f;

std::string_view ToolModeLabel(ToolMode mode) {
  switch (mode) {
    case ToolMode::kBuild:
      return "Build";
    case ToolMode::kDestroy:
      return "Destroy";
    case ToolMode::kCut:
      return "Cut";
    case ToolMode::kCutIn:
      return "Cut in";
  }
  return {};
}

std::optional<flecs::entity> FindLocalPlayer(flecs::world world) {
  const auto* local = world.try_get<z13::gameplay::LocalPlayer>();
  if (local == nullptr || !local->id) {
    return std::nullopt;
  }
  const uint32_t id = *local->id;
  std::optional<flecs::entity> found;
  world.each([&found, id](flecs::entity player, const z13::gameplay::Player& candidate) {
    if (candidate.id == id) {
      found = player;
    }
  });
  return found;
}

// The tool's own mode, or the one a held cut key puts it in (its tags on the player).
std::string_view ItemLabel(flecs::entity item, flecs::entity player, bool active) {
  const auto* tool = item.try_get<z13::building::BuildTool>();
  if (tool == nullptr) {
    return "Item";
  }
  const ToolMode mode = active ? z13::building::EffectiveToolMode(tool->mode, player.has<z13::building::CutModifier>(),
                                                                  player.has<z13::building::CutInModifier>())
                               : tool->mode;
  return ToolModeLabel(mode);
}

void DrawCentered(ImDrawList& draw, const Eigen::Vector2f& center, std::string_view text) {
  const std::string label(text);
  const ImVec2 size = ImGui::CalcTextSize(label.c_str());
  draw.AddText({center.x() - size.x / 2.f, center.y() - size.y / 2.f}, kLabelColor, label.c_str());
}

void DrawCells(ImDrawList& draw, const z13::TouchLayout& layout, const Eigen::Vector2f& screen, const Hotbar& hotbar) {
  for (size_t cell = 0; cell < z13::input::touch::kHotbarCellButtons.size(); ++cell) {
    const Eigen::AlignedBox2f rect = z13::input::touch::CellRect(layout, cell, screen);
    const bool active = cell == hotbar.active_cell;
    const ImVec2 min {rect.min().x(), rect.min().y()};
    const ImVec2 max {rect.max().x(), rect.max().y()};
    draw.AddRectFilled(min, max, active ? kActiveCellColor : kCellColor);
    draw.AddRect(min, max, active ? kActiveOutlineColor : kOutlineColor, 0.f, 0,
                 active ? kActiveOutlineThickness : kOutlineThickness);
    const float inset = rect.sizes().x() * kNumberInset;
    const std::string number(z13::input::touch::kHotbarCellButtons[cell].label);
    draw.AddText({min.x + inset, min.y + inset}, kOutlineColor, number.c_str());
  }
}

}  // namespace

bool LocalPlayerHoldsBuildTool(flecs::world world) {
  const std::optional<flecs::entity> player = FindLocalPlayer(world);
  return player && player->has<z13::building::BuildingTool>();
}

void DrawHotbar(flecs::world world, const z13::TouchLayout& layout) {
  const std::optional<flecs::entity> player = FindLocalPlayer(world);
  if (!player || !player->has<Hotbar>()) {
    return;
  }
  const Hotbar& hotbar = player->get<Hotbar>();
  const uint32_t player_id = player->get<z13::gameplay::Player>().id;
  // The touch layout is in fractions of the screen, so ImGui's own units place it alike.
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const Eigen::Vector2f screen(display.x, display.y);
  ImDrawList& draw = *ImGui::GetBackgroundDrawList();
  DrawCells(draw, layout, screen, hotbar);

  world.each([&](flecs::entity item, const Item&, const CarriedBy& carried) {
    if (carried.player_id != player_id || carried.cell >= z13::input::touch::kHotbarCellButtons.size()) {
      return;
    }
    const bool active = carried.cell == hotbar.active_cell;
    const std::string_view label = ItemLabel(item, *player, active);
    DrawCentered(draw, z13::input::touch::CellRect(layout, carried.cell, screen).center(), label);
    if (active && item.has<z13::building::BuildTool>()) {
      DrawCentered(draw, screen / 2.f + Eigen::Vector2f(0.f, screen.y() * kModeLineOffset), label);
    }
  });
}

}  // namespace z13::raylib::gui
