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

#include <cstdint>

#include <Eigen/Core>

#include <input_config_generated.h>

namespace z13::input {

struct SystemInputEventType {};

struct MousePos {
  int x;
  int y;
};

struct MouseMoveEvent {
  MousePos delta;
};

struct MouseButtonEvent {
  MousePos pos;
  z13::fbs::input::Keycode button;
  uint8_t clicks;
};

struct MouseButtonDownEvent : public MouseButtonEvent {
};

struct MouseButtonUpEvent : MouseButtonEvent {
};

struct Keycode {
  z13::fbs::input::Keycode code;
  int32_t raw_code;
  int8_t mod;
  uint8_t repeat;
};

struct KeyboardEvent {
  Keycode keycode;
};

struct KeyboardDownEvent : KeyboardEvent {
};

struct KeyboardUpEvent : KeyboardEvent {
};

// A finger at `position` pixels of a screen_size window.
struct TouchEvent {
  uint64_t finger {};
  Eigen::Vector2f position = Eigen::Vector2f::Zero();
  Eigen::Vector2f screen_size = Eigen::Vector2f::Zero();
};

struct TouchDownEvent : TouchEvent {
};

struct TouchMoveEvent : TouchEvent {
};

// Also a cancelled touch.
struct TouchUpEvent : TouchEvent {
};

struct SaveConfigEvent {};
struct LoadConfigEvent {};
struct SetDefaultConfigEvent {};
struct AppendInputSchema {};
struct OnConfigUpdatedEvent {};

struct WindowBackEvent {};

struct WindowKeyDownEvent {
  z13::fbs::input::Keycode key_code = z13::fbs::input::Keycode::KEY_UNKNOWN;
};

}  // namespace z13::input
