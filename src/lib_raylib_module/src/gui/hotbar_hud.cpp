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

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <imgui.h>

#include <primitives/palette.h>
#include <touch/touch_mapping.h>
#include <z13/components/block_brush.h>
#include <z13/components/building.h>
#include <z13/components/inventory.h>
#include <z13/components/player.h>
#include <z13/components/tool_mode_info.h>

#include "gui_widgets.h"

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
// The brush row's cells: this part of a hotbar cell tall and at least as wide, padded around the
// short name, this far apart.
constexpr float kBrushCellHeight = 0.6f;
constexpr float kBrushPaddingEm = 0.5f;
constexpr float kBrushGapEm = 0.3f;

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
  return z13::building::ToolModeInfoOf(mode).label;
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

std::optional<flecs::entity> HeldItem(flecs::world world, flecs::entity player) {
  if (!player.has<z13::building::BuildingTool>() || !player.has<z13::inventory::ActiveItem>()) {
    return std::nullopt;
  }
  const flecs::entity item = world.entity(player.get<z13::inventory::ActiveItem>().item);
  return item.is_alive() ? std::optional(item) : std::nullopt;
}

// Rows of slots, bottom up, each at most `max_width` wide counting the gaps.
std::vector<std::vector<size_t>> BrushRows(const std::vector<float>& widths, float max_width, float gap) {
  std::vector<std::vector<size_t>> rows(1);
  float row_width = 0.f;
  for (size_t slot = 0; slot < widths.size(); ++slot) {
    const float added = (rows.back().empty() ? 0.f : gap) + widths[slot];
    if (!rows.back().empty() && row_width + added > max_width) {
      rows.emplace_back();
      row_width = 0.f;
    }
    row_width += rows.back().empty() ? widths[slot] : added;
    rows.back().push_back(slot);
  }
  return rows;
}

}  // namespace

std::vector<Eigen::AlignedBox2f> DrawBrushRow(flecs::world world, const z13::TouchLayout& layout) {
  const std::optional<flecs::entity> player = FindLocalPlayer(world);
  const std::optional<flecs::entity> tool = player ? HeldItem(world, *player) : std::nullopt;
  const auto* brush = tool ? tool->try_get<z13::station::BlockBrush>() : nullptr;
  const auto* palette = world.try_get<z13::building::primitives::BlockPalette>();
  if (brush == nullptr || palette == nullptr) {
    return {};
  }
  const auto& primitives = palette->palette.primitives;
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const Eigen::Vector2f screen(display.x, display.y);
  const Eigen::AlignedBox2f first_cell = z13::input::touch::CellRect(layout, 0, screen);
  const Eigen::AlignedBox2f last_cell =
      z13::input::touch::CellRect(layout, z13::input::touch::kHotbarCellButtons.size() - 1, screen);
  const float height = first_cell.sizes().y() * kBrushCellHeight;
  const float gap = Em(kBrushGapEm);
  const float hotbar_width = last_cell.max().x() - first_cell.min().x();
  const float center_x = (first_cell.min().x() + last_cell.max().x()) / 2.f;

  std::vector<float> widths;
  std::ranges::transform(primitives, std::back_inserter(widths), [height](const auto& primitive) {
    return std::max(height, ImGui::CalcTextSize(primitive.short_name.c_str()).x + Em(kBrushPaddingEm) * 2.f);
  });
  std::vector<Eigen::AlignedBox2f> areas(primitives.size());
  ImDrawList& draw = *ImGui::GetBackgroundDrawList();
  const auto rows = BrushRows(widths, hotbar_width, gap);
  for (size_t row = 0; row < rows.size(); ++row) {
    const float bottom = first_cell.min().y() - gap - static_cast<float>(row) * (height + gap);
    float row_width = gap * static_cast<float>(rows[row].size() - 1);
    for (const size_t slot : rows[row]) {
      row_width += widths[slot];
    }
    float left = center_x - row_width / 2.f;
    for (const size_t slot : rows[row]) {
      const Eigen::AlignedBox2f cell(Eigen::Vector2f(left, bottom - height), Eigen::Vector2f(left + widths[slot], bottom));
      const bool current = primitives[slot].id == brush->spec.type_id;
      draw.AddRectFilled({cell.min().x(), cell.min().y()}, {cell.max().x(), cell.max().y()},
                         current ? kActiveCellColor : kCellColor);
      draw.AddRect({cell.min().x(), cell.min().y()}, {cell.max().x(), cell.max().y()},
                   current ? kActiveOutlineColor : kOutlineColor, 0.f, 0,
                   current ? kActiveOutlineThickness : kOutlineThickness);
      DrawCentered(draw, cell.center(), primitives[slot].short_name);
      areas[slot] = cell;
      left += widths[slot] + gap;
    }
  }
  if (const auto current = palette->palette.Find(brush->spec.type_id)) {
    const float rows_top = first_cell.min().y() - static_cast<float>(rows.size()) * (height + gap);
    DrawCentered(draw, Eigen::Vector2f(center_x, rows_top - height / 2.f), current->get().name);
  }
  return areas;
}

std::optional<std::string_view> LocalToolAction(flecs::world world) {
  const std::optional<flecs::entity> player = FindLocalPlayer(world);
  const std::optional<flecs::entity> item = player ? HeldItem(world, *player) : std::nullopt;
  if (!item) {
    return std::nullopt;
  }
  return ItemLabel(*item, *player, true);
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
