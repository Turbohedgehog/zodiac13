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

#include <touch/touch_mapping.h>

#include <algorithm>
#include <cmath>
#include <ranges>

namespace z13::input::touch {

namespace {

using z13::fbs::input::Keycode;

// The panel's buttons apart by this many radii: a diameter and a gap.
constexpr float kToolPanelStepRadii = 2.2f;

Eigen::Vector2f LayoutPosition(const Layout& layout, TouchControl control) {
  switch (control) {
    case TouchControl::kJump:
      return {layout.jump_x, layout.jump_y};
    case TouchControl::kCrouch:
      return {layout.crouch_x, layout.crouch_y};
    case TouchControl::kUse:
      return {layout.use_x, layout.use_y};
    case TouchControl::kFlashlight:
      return {layout.flashlight_x, layout.flashlight_y};
    case TouchControl::kToolMode:
      return {layout.tool_mode_x, layout.tool_mode_y};
    case TouchControl::kPrimary:
      return {layout.primary_x, layout.primary_y};
    case TouchControl::kDestroy:
      return {layout.destroy_x, layout.destroy_y};
    case TouchControl::kStick:
      [[fallthrough]];
    case TouchControl::kLook:
      [[fallthrough]];
    case TouchControl::kStats:
      [[fallthrough]];
    case TouchControl::kBrush:
      [[fallthrough]];
    case TouchControl::kPreviousPrimitive:
      [[fallthrough]];
    case TouchControl::kNextPrimitive:
      [[fallthrough]];
    case TouchControl::kTurnZ:
      [[fallthrough]];
    case TouchControl::kTurnY:
      [[fallthrough]];
    case TouchControl::kTurnX:
      [[fallthrough]];
    case TouchControl::kThicker:
      [[fallthrough]];
    case TouchControl::kThinner:
      [[fallthrough]];
    case TouchControl::kMergeAt:
      [[fallthrough]];
    case TouchControl::kFlipDiagonal:
      [[fallthrough]];
    case TouchControl::kCell1:
      [[fallthrough]];
    case TouchControl::kCell2:
      [[fallthrough]];
    case TouchControl::kCell3:
      [[fallthrough]];
    case TouchControl::kCell4:
      [[fallthrough]];
    case TouchControl::kCell5:
      [[fallthrough]];
    case TouchControl::kCell6:
      [[fallthrough]];
    case TouchControl::kCell7:
      [[fallthrough]];
    case TouchControl::kCell8:
      [[fallthrough]];
    case TouchControl::kCell9:
      break;
  }
  return Eigen::Vector2f::Zero();
}

float Quantized(float value, float step) {
  return std::clamp(std::round(value / step) * step, -1.f, 1.f);
}

}  // namespace

std::optional<Keycode> ButtonKey(TouchControl control) {
  if (const auto button = std::ranges::find(kTouchButtons, control, &TouchButton::control);
      button != kTouchButtons.end()) {
    return button->key;
  }
  if (const auto cell = std::ranges::find(kHotbarCellButtons, control, &TouchButton::control);
      cell != kHotbarCellButtons.end()) {
    return cell->key;
  }
  return std::nullopt;
}

std::span<const TouchControl> PanelOf(TouchLayer layer) {
  switch (layer) {
    case TouchLayer::kBlock:
      return kBlockPanel;
    case TouchLayer::kWall:
      return kWallPanel;
    case TouchLayer::kMerge:
      return kMergePanel;
    case TouchLayer::kBridge:
      return kBridgePanel;
    case TouchLayer::kWalk:
      break;
  }
  return {};
}

bool InLayer(TouchLayer layer, TouchControl control) {
  if (std::ranges::contains(kToolButtons, control)) {
    return layer != TouchLayer::kWalk;
  }
  if (std::ranges::contains(PanelOf(layer), control)) {
    return true;
  }
  constexpr std::array kPanelLayers {TouchLayer::kBlock, TouchLayer::kWall, TouchLayer::kMerge, TouchLayer::kBridge};
  return !std::ranges::any_of(kPanelLayers,
                              [control](TouchLayer other) { return std::ranges::contains(PanelOf(other), control); });
}

Eigen::Vector2f ButtonCenter(const Layout& layout, TouchLayer layer, TouchControl control,
                             const Eigen::Vector2f& screen_size) {
  const std::span<const TouchControl> panel = PanelOf(layer);
  if (const auto slot = std::ranges::find(panel, control); slot != panel.end()) {
    const float step = kToolPanelStepRadii * ButtonRadius(layout, screen_size);
    const Eigen::Vector2f first = Eigen::Vector2f(layout.tool_panel_x, layout.tool_panel_y).cwiseProduct(screen_size);
    return first + Eigen::Vector2f(step * static_cast<float>(slot - panel.begin()), 0.f);
  }
  return LayoutPosition(layout, control).cwiseProduct(screen_size);
}

float ButtonRadius(const Layout& layout, const Eigen::Vector2f& screen_size) {
  return layout.button_radius * screen_size.y();
}

Eigen::AlignedBox2f CellRect(const Layout& layout, size_t cell, const Eigen::Vector2f& screen_size) {
  const float side = layout.hotbar_cell_size * screen_size.y();
  const float row_left = (screen_size.x() - side * static_cast<float>(kHotbarCellButtons.size())) / 2.f;
  const Eigen::Vector2f min(row_left + side * static_cast<float>(cell), layout.hotbar_y * screen_size.y() - side / 2.f);
  return {min, min + Eigen::Vector2f::Constant(side)};
}

float StickRadius(const Layout& layout, const Eigen::Vector2f& screen_size) {
  return layout.stick_radius * screen_size.y();
}

TouchControl ControlAt(const Layout& layout, TouchLayer layer, const Eigen::Vector2f& position,
                       const Eigen::Vector2f& screen_size) {
  for (size_t cell = 0; cell < kHotbarCellButtons.size(); ++cell) {
    if (CellRect(layout, cell, screen_size).contains(position)) {
      return kHotbarCellButtons[cell].control;
    }
  }
  const float radius = ButtonRadius(layout, screen_size);
  const auto button = std::ranges::find_if(kTouchButtons, [&](const TouchButton& candidate) {
    return InLayer(layer, candidate.control) &&
           (ButtonCenter(layout, layer, candidate.control, screen_size) - position).norm() <= radius;
  });
  if (button != kTouchButtons.end()) {
    return button->control;
  }
  return position.x() < layout.stick_zone_width * screen_size.x() ? TouchControl::kStick : TouchControl::kLook;
}

Eigen::Vector2f StickValue(const Layout& layout, const TouchFinger& finger, const Eigen::Vector2f& screen_size) {
  // Screen y grows downwards; forward is up.
  const Eigen::Vector2f offset = (finger.position - finger.origin).cwiseProduct(Eigen::Vector2f(1.f, -1.f)) /
                                 StickRadius(layout, screen_size);
  const float deflection = std::min(offset.norm(), 1.f);
  if (deflection <= layout.stick_dead_zone) {
    return Eigen::Vector2f::Zero();
  }
  const float past_dead_zone = (deflection - layout.stick_dead_zone) / (1.f - layout.stick_dead_zone);
  const Eigen::Vector2f value = offset.normalized() * past_dead_zone;
  return {Quantized(value.x(), layout.stick_step), Quantized(value.y(), layout.stick_step)};
}

std::array<KeyTransition, 4> StickKeys(const Eigen::Vector2f& value) {
  return {
      KeyTransition {.key = Keycode::TOUCH_STICK_UP, .value = std::max(value.y(), 0.f)},
      KeyTransition {.key = Keycode::TOUCH_STICK_DOWN, .value = std::max(-value.y(), 0.f)},
      KeyTransition {.key = Keycode::TOUCH_STICK_LEFT, .value = std::max(-value.x(), 0.f)},
      KeyTransition {.key = Keycode::TOUCH_STICK_RIGHT, .value = std::max(value.x(), 0.f)},
  };
}

Eigen::Vector2f LookDegrees(const Layout& layout, const Eigen::Vector2f& delta, const Eigen::Vector2f& screen_size) {
  return -delta / screen_size.y() * layout.look_degrees;
}

void ScheduleKey(InputState& input_state, Keycode key, float value) {
  const auto queued = std::ranges::find_last(input_state.pending, key, &KeyTransition::key);
  const float last = queued.empty() ? input_state.input_state[KeyIndex(key)] : queued.front().value;
  if (last == value) {
    return;
  }
  if (!queued.empty() && last != 0.f && value != 0.f) {
    queued.front().value = value;
    return;
  }
  input_state.pending.push_back({.key = key, .value = value});
}

}  // namespace z13::input::touch
