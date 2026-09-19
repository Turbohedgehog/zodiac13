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

#include <z13/components/input.h>

#include "z13_test_world.h"

namespace z13::testing {

constexpr float kTestDeltaTime = 1.f;

inline z13::input::KeyboardDownEvent KeyDown(z13::fbs::input::Keycode code) {
  z13::input::KeyboardDownEvent event;
  event.keycode.code = code;
  return event;
}

inline z13::input::KeyboardUpEvent KeyUp(z13::fbs::input::Keycode code) {
  z13::input::KeyboardUpEvent event;
  event.keycode.code = code;
  return event;
}

inline z13::input::MouseButtonDownEvent MouseDown(z13::fbs::input::Keycode button) {
  z13::input::MouseButtonDownEvent event;
  event.button = button;
  return event;
}

inline z13::input::MouseButtonUpEvent MouseUp(z13::fbs::input::Keycode button) {
  z13::input::MouseButtonUpEvent event;
  event.button = button;
  return event;
}

// A full press-and-release, spread across two frames so the edge-triggered
// ActionValueHolder in the action pipeline sees a real 0 -> 1 -> 0 sequence
// (see ApplyBuildActionListener / ActionValueHolder::IsSwitchedOn).
inline void Click(Z13TestWorld& test_world, z13::fbs::input::Keycode button) {
  test_world.EmitInput(MouseDown(button));
  test_world.World().progress(kTestDeltaTime);
  test_world.EmitInput(MouseUp(button));
  test_world.World().progress(kTestDeltaTime);
}

inline void EnterBuildMode(Z13TestWorld& test_world) {
  test_world.EmitInput(KeyDown(z13::fbs::input::Keycode::KEY_TAB));
  test_world.World().progress(kTestDeltaTime);
}

// Releases and presses TAB again, so the edge-triggered toggle fires whether or not
// TAB is still held from an earlier EnterBuildMode.
inline void ToggleBuildMode(Z13TestWorld& test_world) {
  test_world.EmitInput(KeyUp(z13::fbs::input::Keycode::KEY_TAB));
  test_world.World().progress(kTestDeltaTime);
  EnterBuildMode(test_world);
}

}  // namespace z13::testing
