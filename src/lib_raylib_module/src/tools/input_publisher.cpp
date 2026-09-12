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

#include "input_publisher.h"

#include <cstdint>
#include <unordered_map>

#include <SDL3/SDL.h>
#include <flecs.h>

#include <lib_core/components.h>
#include <lib_core/flecs_utils.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include <input_config_generated.h>

#include <raylib_module/raylib_components.h>

#include "platform/sdl_platform.h"

namespace z13::raylib {

namespace {

namespace zkey = z13::fbs::input;

constexpr const char* kInputSourceName = "z13::raylib::InputEventSource";

// SDL keycode -> z13 Keycode. The z13 enum names mirror SDL's, so this is a
// straight rename table; unlisted keys still reach systems via RaylibInputFrame.
const std::unordered_map<SDL_Keycode, zkey::Keycode>& KeyMap() {
  static const std::unordered_map<SDL_Keycode, zkey::Keycode> map = {
      {SDLK_A, zkey::Keycode::KEY_A}, {SDLK_B, zkey::Keycode::KEY_B},
      {SDLK_C, zkey::Keycode::KEY_C}, {SDLK_D, zkey::Keycode::KEY_D},
      {SDLK_E, zkey::Keycode::KEY_E}, {SDLK_F, zkey::Keycode::KEY_F},
      {SDLK_G, zkey::Keycode::KEY_G}, {SDLK_H, zkey::Keycode::KEY_H},
      {SDLK_I, zkey::Keycode::KEY_I}, {SDLK_J, zkey::Keycode::KEY_J},
      {SDLK_K, zkey::Keycode::KEY_K}, {SDLK_L, zkey::Keycode::KEY_L},
      {SDLK_M, zkey::Keycode::KEY_M}, {SDLK_N, zkey::Keycode::KEY_N},
      {SDLK_O, zkey::Keycode::KEY_O}, {SDLK_P, zkey::Keycode::KEY_P},
      {SDLK_Q, zkey::Keycode::KEY_Q}, {SDLK_R, zkey::Keycode::KEY_R},
      {SDLK_S, zkey::Keycode::KEY_S}, {SDLK_T, zkey::Keycode::KEY_T},
      {SDLK_U, zkey::Keycode::KEY_U}, {SDLK_V, zkey::Keycode::KEY_V},
      {SDLK_W, zkey::Keycode::KEY_W}, {SDLK_X, zkey::Keycode::KEY_X},
      {SDLK_Y, zkey::Keycode::KEY_Y}, {SDLK_Z, zkey::Keycode::KEY_Z},
      {SDLK_0, zkey::Keycode::KEY_0}, {SDLK_1, zkey::Keycode::KEY_1},
      {SDLK_2, zkey::Keycode::KEY_2}, {SDLK_3, zkey::Keycode::KEY_3},
      {SDLK_4, zkey::Keycode::KEY_4}, {SDLK_5, zkey::Keycode::KEY_5},
      {SDLK_6, zkey::Keycode::KEY_6}, {SDLK_7, zkey::Keycode::KEY_7},
      {SDLK_8, zkey::Keycode::KEY_8}, {SDLK_9, zkey::Keycode::KEY_9},
      {SDLK_F1, zkey::Keycode::KEY_F1}, {SDLK_F2, zkey::Keycode::KEY_F2},
      {SDLK_F3, zkey::Keycode::KEY_F3}, {SDLK_F4, zkey::Keycode::KEY_F4},
      {SDLK_F5, zkey::Keycode::KEY_F5}, {SDLK_F6, zkey::Keycode::KEY_F6},
      {SDLK_F7, zkey::Keycode::KEY_F7}, {SDLK_F8, zkey::Keycode::KEY_F8},
      {SDLK_F9, zkey::Keycode::KEY_F9}, {SDLK_F10, zkey::Keycode::KEY_F10},
      {SDLK_F11, zkey::Keycode::KEY_F11}, {SDLK_F12, zkey::Keycode::KEY_F12},
      {SDLK_UP, zkey::Keycode::KEY_UP}, {SDLK_DOWN, zkey::Keycode::KEY_DOWN},
      {SDLK_LEFT, zkey::Keycode::KEY_LEFT}, {SDLK_RIGHT, zkey::Keycode::KEY_RIGHT},
      {SDLK_SPACE, zkey::Keycode::KEY_SPACE}, {SDLK_ESCAPE, zkey::Keycode::KEY_ESCAPE},
      {SDLK_RETURN, zkey::Keycode::KEY_RETURN}, {SDLK_TAB, zkey::Keycode::KEY_TAB},
      {SDLK_BACKSPACE, zkey::Keycode::KEY_BACKSPACE}, {SDLK_DELETE, zkey::Keycode::KEY_DELETE},
      {SDLK_INSERT, zkey::Keycode::KEY_INSERT}, {SDLK_HOME, zkey::Keycode::KEY_HOME},
      {SDLK_END, zkey::Keycode::KEY_END}, {SDLK_PAGEUP, zkey::Keycode::KEY_PAGEUP},
      {SDLK_PAGEDOWN, zkey::Keycode::KEY_PAGEDOWN},
      {SDLK_LSHIFT, zkey::Keycode::KEY_LSHIFT}, {SDLK_LCTRL, zkey::Keycode::KEY_LCTRL},
      {SDLK_LALT, zkey::Keycode::KEY_LALT}, {SDLK_RSHIFT, zkey::Keycode::KEY_RSHIFT},
      {SDLK_RCTRL, zkey::Keycode::KEY_RCTRL}, {SDLK_RALT, zkey::Keycode::KEY_RALT},
      {SDLK_MINUS, zkey::Keycode::KEY_MINUS}, {SDLK_EQUALS, zkey::Keycode::KEY_EQUALS},
      {SDLK_LEFTBRACKET, zkey::Keycode::KEY_LEFTBRACKET},
      {SDLK_RIGHTBRACKET, zkey::Keycode::KEY_RIGHTBRACKET},
      {SDLK_BACKSLASH, zkey::Keycode::KEY_BACKSLASH}, {SDLK_SEMICOLON, zkey::Keycode::KEY_SEMICOLON},
      {SDLK_APOSTROPHE, zkey::Keycode::KEY_QUOTE}, {SDLK_GRAVE, zkey::Keycode::KEY_BACKQUOTE},
      {SDLK_COMMA, zkey::Keycode::KEY_COMMA}, {SDLK_PERIOD, zkey::Keycode::KEY_PERIOD},
      {SDLK_SLASH, zkey::Keycode::KEY_SLASH},
  };
  return map;
}

zkey::Keycode MouseButtonToKeycode(Uint8 sdl_button) {
  switch (sdl_button) {
    case SDL_BUTTON_LEFT:
      return zkey::Keycode::MOUSE_BUTTON_LEFT;
    case SDL_BUTTON_RIGHT:
      return zkey::Keycode::MOUSE_BUTTON_RIGHT;
    case SDL_BUTTON_MIDDLE:
      return zkey::Keycode::MOUSE_BUTTON_MIDDLE;
    case SDL_BUTTON_X1:
      return zkey::Keycode::MOUSE_BUTTON_X1;
    case SDL_BUTTON_X2:
      return zkey::Keycode::MOUSE_BUTTON_X2;
    default:
      return zkey::Keycode::MOUSE_BUTTON_UNKNOWN;
  }
}

template <typename EventT>
void Emit(flecs::world world, flecs::entity source, const EventT& event) {
  source.set<EventT>(event);
  world.event<z13::input::SystemInputEventType>().id<EventT>().entity(source).emit();
}

void ReadInput(flecs::world world) {
  z13::WorldNoDeferGuard no_defer(world);
  flecs::entity source = world.entity(kInputSourceName);

  int mouse_x = 0;
  int mouse_y = 0;
  SdlPlatform::MousePosition(&mouse_x, &mouse_y);
  const z13::input::MousePos position{mouse_x, mouse_y};

  int event_count = 0;
  const SDL_Event* events = SdlPlatform::FrameEvents(&event_count);
  for (int i = 0; i < event_count; ++i) {
    const SDL_Event& event = events[i];
    switch (event.type) {
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: {
        const auto it = KeyMap().find(event.key.key);
        if (it == KeyMap().end()) {
          break;
        }
        z13::input::Keycode keycode{};
        keycode.code = it->second;
        keycode.raw_code = static_cast<int32_t>(event.key.key);
        keycode.mod = 0;
        keycode.repeat = event.key.repeat ? 1 : 0;
        if (event.type == SDL_EVENT_KEY_DOWN) {
          Emit(world, source, z13::input::KeyboardDownEvent{{keycode}});
        } else {
          Emit(world, source, z13::input::KeyboardUpEvent{{keycode}});
        }
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        z13::input::MouseButtonEvent button{};
        button.pos = position;
        button.button = MouseButtonToKeycode(event.button.button);
        button.clicks = event.button.clicks;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
          Emit(world, source, z13::input::MouseButtonDownEvent{button});
        } else {
          Emit(world, source, z13::input::MouseButtonUpEvent{button});
        }
        break;
      }
      case SDL_EVENT_MOUSE_MOTION: {
        Emit(world, source,
             z13::input::MousePos{static_cast<int>(event.motion.x),
                                  static_cast<int>(event.motion.y)});
        z13::input::MouseMoveEvent move{};
        move.delta = {static_cast<int>(event.motion.xrel), static_cast<int>(event.motion.yrel)};
        Emit(world, source, move);
        break;
      }
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        world.set(z13::gameplay::WindowFocusEvent{.has_focus = true});
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        world.set(z13::gameplay::WindowFocusEvent{.has_focus = false});
        break;
      default:
        break;
    }
  }

  int dx = 0;
  int dy = 0;
  SdlPlatform::MouseDelta(&dx, &dy);
  RaylibInputFrame frame{};
  frame.mouse_x = static_cast<float>(mouse_x);
  frame.mouse_y = static_cast<float>(mouse_y);
  frame.mouse_dx = static_cast<float>(dx);
  frame.mouse_dy = static_cast<float>(dy);
  frame.mouse_wheel = 0.f;
  for (int i = 0; i < event_count; ++i) {
    if (events[i].type == SDL_EVENT_MOUSE_WHEEL) {
      frame.mouse_wheel += events[i].wheel.y;
    }
  }
  world.set<RaylibInputFrame>(frame);

  // Relative-mouse look during gameplay; free cursor while paused.
  SdlPlatform::SetRelativeMouse(!world.has<z13::gameplay::Pause>());
}

void RegisterComponents(flecs::world world) {
  world.component<RaylibInputFrame>().add(flecs::Singleton);
}

void RegisterSystems(flecs::world world) {
  // .immediate(): ReadInput emits events synchronously (WorldNoDeferGuard), which
  // is only legal from a non-deferred system -- same as the Ogre ReadEventsSystem.
  world.system<const RaylibData>("InputPublisher::ReadInput")
      .kind<ReadEvents>()
      .immediate()
      .each([world](const RaylibData&) { ReadInput(world); });
}

void CreateDefaults(flecs::world world) {
  world.entity(kInputSourceName);
  world.set<RaylibInputFrame>({});
  // RaylibSystem::CreateDefaults runs first and may have already torn down SDL
  // (and called Core::Shutdown(), which only sets a pending flag) on init failure.
  if (!SdlPlatform::IsReady()) return;
  SdlPlatform::SetRelativeMouse(!world.has<z13::gameplay::Pause>());
}

}  // namespace

void InputPublisher::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<InitSystemsEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });

  world.observer<InitWorldDataEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { CreateDefaults(world); });
}

}  // namespace z13::raylib
