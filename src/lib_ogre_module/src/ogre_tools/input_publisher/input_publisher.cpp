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

module;

#include <z13/components/input.h>
#include <SDL2/SDL.h>

#include "OgreInput.h"
#include "OgreBitesPrerequisites.h"

#include <lib_core/log.h>

#include <input_config_generated.h>

module z13.ogre.input_publisher;

import <array>;
import <algorithm>;
import <optional>;
import <map>;

namespace z13::ogre {

static const std::map<SDL_KeyCode, z13::fbs::input::Keycode> kSdlToProtoKeyCode = {
  { SDLK_UNKNOWN, z13::fbs::input::Keycode::KEY_UNKNOWN },
  { SDLK_RETURN, z13::fbs::input::Keycode::KEY_RETURN },
  { SDLK_ESCAPE, z13::fbs::input::Keycode::KEY_ESCAPE },
  { SDLK_BACKSPACE, z13::fbs::input::Keycode::KEY_BACKSPACE },
  { SDLK_TAB, z13::fbs::input::Keycode::KEY_TAB },
  { SDLK_SPACE, z13::fbs::input::Keycode::KEY_SPACE },
  { SDLK_EXCLAIM, z13::fbs::input::Keycode::KEY_EXCLAIM },
  { SDLK_QUOTEDBL, z13::fbs::input::Keycode::KEY_QUOTEDBL },
  { SDLK_HASH, z13::fbs::input::Keycode::KEY_HASH },
  { SDLK_PERCENT, z13::fbs::input::Keycode::KEY_PERCENT },
  { SDLK_DOLLAR, z13::fbs::input::Keycode::KEY_DOLLAR },
  { SDLK_AMPERSAND, z13::fbs::input::Keycode::KEY_AMPERSAND },
  { SDLK_QUOTE, z13::fbs::input::Keycode::KEY_QUOTE },
  { SDLK_LEFTPAREN, z13::fbs::input::Keycode::KEY_LEFTPAREN },
  { SDLK_RIGHTPAREN, z13::fbs::input::Keycode::KEY_RIGHTPAREN },
  { SDLK_ASTERISK, z13::fbs::input::Keycode::KEY_ASTERISK },
  { SDLK_PLUS, z13::fbs::input::Keycode::KEY_PLUS },
  { SDLK_COMMA, z13::fbs::input::Keycode::KEY_COMMA },
  { SDLK_MINUS, z13::fbs::input::Keycode::KEY_MINUS },
  { SDLK_PERIOD, z13::fbs::input::Keycode::KEY_PERIOD },
  { SDLK_SLASH, z13::fbs::input::Keycode::KEY_SLASH },
  { SDLK_0, z13::fbs::input::Keycode::KEY_0 },
  { SDLK_1, z13::fbs::input::Keycode::KEY_1 },
  { SDLK_2, z13::fbs::input::Keycode::KEY_2 },
  { SDLK_3, z13::fbs::input::Keycode::KEY_3 },
  { SDLK_4, z13::fbs::input::Keycode::KEY_4 },
  { SDLK_5, z13::fbs::input::Keycode::KEY_5 },
  { SDLK_6, z13::fbs::input::Keycode::KEY_6 },
  { SDLK_7, z13::fbs::input::Keycode::KEY_7 },
  { SDLK_8, z13::fbs::input::Keycode::KEY_8 },
  { SDLK_9, z13::fbs::input::Keycode::KEY_9 },
  { SDLK_COLON, z13::fbs::input::Keycode::KEY_COLON },
  { SDLK_SEMICOLON, z13::fbs::input::Keycode::KEY_SEMICOLON },
  { SDLK_LESS, z13::fbs::input::Keycode::KEY_LESS },
  { SDLK_EQUALS, z13::fbs::input::Keycode::KEY_EQUALS },
  { SDLK_GREATER, z13::fbs::input::Keycode::KEY_GREATER },
  { SDLK_QUESTION, z13::fbs::input::Keycode::KEY_QUESTION },
  { SDLK_AT, z13::fbs::input::Keycode::KEY_AT },
  { SDLK_LEFTBRACKET, z13::fbs::input::Keycode::KEY_LEFTBRACKET },
  { SDLK_BACKSLASH, z13::fbs::input::Keycode::KEY_BACKSLASH },
  { SDLK_RIGHTBRACKET, z13::fbs::input::Keycode::KEY_RIGHTBRACKET },
  { SDLK_CARET, z13::fbs::input::Keycode::KEY_CARET },
  { SDLK_UNDERSCORE, z13::fbs::input::Keycode::KEY_UNDERSCORE },
  { SDLK_BACKQUOTE, z13::fbs::input::Keycode::KEY_BACKQUOTE },
  { SDLK_a, z13::fbs::input::Keycode::KEY_A },
  { SDLK_b, z13::fbs::input::Keycode::KEY_B },
  { SDLK_c, z13::fbs::input::Keycode::KEY_C },
  { SDLK_d, z13::fbs::input::Keycode::KEY_D },
  { SDLK_e, z13::fbs::input::Keycode::KEY_E },
  { SDLK_f, z13::fbs::input::Keycode::KEY_F },
  { SDLK_g, z13::fbs::input::Keycode::KEY_G },
  { SDLK_h, z13::fbs::input::Keycode::KEY_H },
  { SDLK_i, z13::fbs::input::Keycode::KEY_I },
  { SDLK_j, z13::fbs::input::Keycode::KEY_J },
  { SDLK_k, z13::fbs::input::Keycode::KEY_K },
  { SDLK_l, z13::fbs::input::Keycode::KEY_L },
  { SDLK_m, z13::fbs::input::Keycode::KEY_M },
  { SDLK_n, z13::fbs::input::Keycode::KEY_N },
  { SDLK_o, z13::fbs::input::Keycode::KEY_O },
  { SDLK_p, z13::fbs::input::Keycode::KEY_P },
  { SDLK_q, z13::fbs::input::Keycode::KEY_Q },
  { SDLK_r, z13::fbs::input::Keycode::KEY_R },
  { SDLK_s, z13::fbs::input::Keycode::KEY_S },
  { SDLK_t, z13::fbs::input::Keycode::KEY_T },
  { SDLK_u, z13::fbs::input::Keycode::KEY_U },
  { SDLK_v, z13::fbs::input::Keycode::KEY_V },
  { SDLK_w, z13::fbs::input::Keycode::KEY_W },
  { SDLK_x, z13::fbs::input::Keycode::KEY_X },
  { SDLK_y, z13::fbs::input::Keycode::KEY_Y },
  { SDLK_z, z13::fbs::input::Keycode::KEY_Z },
  { SDLK_CAPSLOCK, z13::fbs::input::Keycode::KEY_CAPSLOCK },
  { SDLK_F1, z13::fbs::input::Keycode::KEY_F1 },
  { SDLK_F2, z13::fbs::input::Keycode::KEY_F2 },
  { SDLK_F3, z13::fbs::input::Keycode::KEY_F3 },
  { SDLK_F4, z13::fbs::input::Keycode::KEY_F4 },
  { SDLK_F5, z13::fbs::input::Keycode::KEY_F5 },
  { SDLK_F6, z13::fbs::input::Keycode::KEY_F6 },
  { SDLK_F7, z13::fbs::input::Keycode::KEY_F7 },
  { SDLK_F8, z13::fbs::input::Keycode::KEY_F8 },
  { SDLK_F9, z13::fbs::input::Keycode::KEY_F9 },
  { SDLK_F10, z13::fbs::input::Keycode::KEY_F10 },
  { SDLK_F11, z13::fbs::input::Keycode::KEY_F11 },
  { SDLK_F12, z13::fbs::input::Keycode::KEY_F12 },
  { SDLK_PRINTSCREEN, z13::fbs::input::Keycode::KEY_PRINTSCREEN },
  { SDLK_SCROLLLOCK, z13::fbs::input::Keycode::KEY_SCROLLLOCK },
  { SDLK_PAUSE, z13::fbs::input::Keycode::KEY_PAUSE },
  { SDLK_INSERT, z13::fbs::input::Keycode::KEY_INSERT },
  { SDLK_HOME, z13::fbs::input::Keycode::KEY_HOME },
  { SDLK_PAGEUP, z13::fbs::input::Keycode::KEY_PAGEUP },
  { SDLK_DELETE, z13::fbs::input::Keycode::KEY_DELETE },
  { SDLK_END, z13::fbs::input::Keycode::KEY_END },
  { SDLK_PAGEDOWN, z13::fbs::input::Keycode::KEY_PAGEDOWN },
  { SDLK_RIGHT, z13::fbs::input::Keycode::KEY_RIGHT },
  { SDLK_LEFT, z13::fbs::input::Keycode::KEY_LEFT },
  { SDLK_DOWN, z13::fbs::input::Keycode::KEY_DOWN },
  { SDLK_UP, z13::fbs::input::Keycode::KEY_UP },
  { SDLK_NUMLOCKCLEAR, z13::fbs::input::Keycode::KEY_NUMLOCKCLEAR },
  { SDLK_KP_DIVIDE, z13::fbs::input::Keycode::KEY_KP_DIVIDE },
  { SDLK_KP_MULTIPLY, z13::fbs::input::Keycode::KEY_KP_MULTIPLY },
  { SDLK_KP_MINUS, z13::fbs::input::Keycode::KEY_KP_MINUS },
  { SDLK_KP_PLUS, z13::fbs::input::Keycode::KEY_KP_PLUS },
  { SDLK_KP_ENTER, z13::fbs::input::Keycode::KEY_KP_ENTER },
  { SDLK_KP_1, z13::fbs::input::Keycode::KEY_KP_1 },
  { SDLK_KP_2, z13::fbs::input::Keycode::KEY_KP_2 },
  { SDLK_KP_3, z13::fbs::input::Keycode::KEY_KP_3 },
  { SDLK_KP_4, z13::fbs::input::Keycode::KEY_KP_4 },
  { SDLK_KP_5, z13::fbs::input::Keycode::KEY_KP_5 },
  { SDLK_KP_6, z13::fbs::input::Keycode::KEY_KP_6 },
  { SDLK_KP_7, z13::fbs::input::Keycode::KEY_KP_7 },
  { SDLK_KP_8, z13::fbs::input::Keycode::KEY_KP_8 },
  { SDLK_KP_9, z13::fbs::input::Keycode::KEY_KP_9 },
  { SDLK_KP_0, z13::fbs::input::Keycode::KEY_KP_0 },
  { SDLK_KP_PERIOD, z13::fbs::input::Keycode::KEY_KP_PERIOD },
  { SDLK_LCTRL, z13::fbs::input::Keycode::KEY_LCTRL },
  { SDLK_LSHIFT, z13::fbs::input::Keycode::KEY_LSHIFT },
  { SDLK_LALT, z13::fbs::input::Keycode::KEY_LALT },
  { SDLK_LGUI, z13::fbs::input::Keycode::KEY_LGUI },
  { SDLK_RCTRL, z13::fbs::input::Keycode::KEY_RCTRL },
  { SDLK_RSHIFT, z13::fbs::input::Keycode::KEY_RSHIFT },
  { SDLK_RALT, z13::fbs::input::Keycode::KEY_RALT },
  { SDLK_RGUI, z13::fbs::input::Keycode::KEY_RGUI },
};

static const std::map<unsigned char, z13::fbs::input::Keycode> kSdlToProtoMouseCode = {
  { SDL_BUTTON_LEFT, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT },
  { SDL_BUTTON_MIDDLE, z13::fbs::input::Keycode::MOUSE_BUTTON_MIDDLE },
  { SDL_BUTTON_RIGHT, z13::fbs::input::Keycode::MOUSE_BUTTON_RIGHT },
  { SDL_BUTTON_X1, z13::fbs::input::Keycode::MOUSE_BUTTON_X1 },
  { SDL_BUTTON_X2, z13::fbs::input::Keycode::MOUSE_BUTTON_X2 },
};

z13::fbs::input::Keycode SdlKeyCodeToZ13Proto(SDL_KeyCode key) {
  auto it = kSdlToProtoKeyCode.find(key);
  return it != kSdlToProtoKeyCode.end() ? it->second : z13::fbs::input::Keycode::KEY_UNKNOWN;
}

z13::fbs::input::Keycode SdlMouseCodeToZ13Proto(unsigned char mouse_button) {
  auto it = kSdlToProtoMouseCode.find(mouse_button);
  return it != kSdlToProtoMouseCode.end() ? it->second : z13::fbs::input::Keycode::MOUSE_BUTTON_UNKNOWN;
}

template <typename MouseEvent, OgreBites::EventType Type>
bool TryPublishMouseButtonEvent(flecs::world world, const OgreBites::Event& ogre_event) {
  if (ogre_event.type != Type) {
    return false;
  }

  const auto& button = ogre_event.button;
  auto mouse_button = SdlMouseCodeToZ13Proto(button.button);

  WorldNoDeferGuard no_defer(world);
  world.event<input::SystemInputEventType>()
    .id<MouseEvent>()
    .entity(world.entity().set(
      MouseEvent ({
        .pos = {button.x, button.y,},
        .button = mouse_button,
        .clicks = button.clicks,
      })
    ))
    .emit();

  return true;
}

bool TryPublishMouseMoveEvent(flecs::world world, const OgreBites::Event& ogre_event) {
  if (ogre_event.type != OgreBites::MOUSEMOTION) {
    return false;
  }

  const auto& motion = ogre_event.motion;

  // LOG_INFO("~~~ TryPublishMouseMoveEvent 1");

  WorldNoDeferGuard no_defer(world);
  world.event<input::SystemInputEventType>()
    .id<input::MousePos>()
    .entity(world.entity().set(
      input::MousePos {
        .x = motion.x,
        .y = motion.y,
      }
    ))
    .emit();

  world.event<input::SystemInputEventType>()
    .id<input::MouseMoveEvent>()
    .entity(world.entity().set(
      input::MouseMoveEvent {
        .delta = {
          .x = motion.xrel,
          .y = motion.yrel,
        },
      }
    ))
    .emit();

  // LOG_INFO("~~~ TryPublishMouseMoveEvent 2");

  return true;
}

template <typename KeyboardEvent, OgreBites::EventType Type>
bool TryPublishKeyboardEvent(flecs::world world, const OgreBites::Event& ogre_event) {
  if (ogre_event.type != Type) {
    return false;
  }


  const auto& key = ogre_event.key;

  WorldNoDeferGuard no_defer(world);

  KeyboardEvent keyboard_event;
  keyboard_event.keycode.code = SdlKeyCodeToZ13Proto(static_cast<SDL_KeyCode>(key.keysym.sym));
  keyboard_event.keycode.raw_code = static_cast<decltype(input::Keycode::raw_code)>(key.keysym.sym);
  keyboard_event.keycode.mod = static_cast<decltype(input::Keycode::mod)>(key.keysym.mod);
  keyboard_event.keycode.repeat = static_cast<decltype(input::Keycode::repeat)>(key.repeat);

  world.event<input::SystemInputEventType>()
    .id<KeyboardEvent>()
    .entity(world.entity().set(keyboard_event))
    .emit();

  // LOG_INFO("~~~ TryPublishKeyboardEvent 2");

  return true;
}

bool InputPublisher::PublishInput(flecs::world world, const OgreBites::Event& ogre_event) {
  static constexpr std::array kPublishers = {
    &TryPublishMouseMoveEvent,
    &TryPublishMouseButtonEvent<input::MouseButtonDownEvent, OgreBites::MOUSEBUTTONDOWN>,
    &TryPublishMouseButtonEvent<input::MouseButtonUpEvent, OgreBites::MOUSEBUTTONUP>,
    &TryPublishKeyboardEvent<input::KeyboardDownEvent, OgreBites::KEYDOWN>,
    &TryPublishKeyboardEvent<input::KeyboardUpEvent, OgreBites::KEYUP>,
  };

  return std::any_of(
    kPublishers.begin(),
    kPublishers.end(),
    [&](const auto& func) {
      return func(world, ogre_event);
    }
  );
}

}  // namespace z13::ogre
