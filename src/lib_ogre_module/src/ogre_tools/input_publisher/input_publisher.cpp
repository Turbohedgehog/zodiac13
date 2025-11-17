#include "input_publisher.h"

#include <array>
#include <algorithm>
#include <optional>

#include <z13_module/components/input.h>
#include <SDL2/SDL.h>

#include <lib_core/log.h>

namespace z13::ogre {

std::optional<z13::input::MouseButton> SdlMouseButtonToInputMouseButton(unsigned char button) {
  switch (button) {
    case SDL_BUTTON_LEFT:
      return z13::input::MouseButton::kLeft;
    case SDL_BUTTON_MIDDLE:
      return z13::input::MouseButton::kMiddle;
    case SDL_BUTTON_RIGHT:
      return z13::input::MouseButton::kRight;
  }

  return std::nullopt;
}

template <typename MouseEvent, OgreBites::EventType Type>
bool TryPublishMouseButtonEvent(flecs::world world, const OgreBites::Event& ogre_event) {
  if (ogre_event.type != Type) {
    return false;
  }

  const auto& button = ogre_event.button;
  const auto& mouse_button = SdlMouseButtonToInputMouseButton(button.button);
  if (!mouse_button) {
    return false;
  }

  world.event<input::SystemInputEvent>()
    .id<MouseEvent>()
    .entity(world.entity().set(
      MouseEvent ({
        .pos = {button.x, button.y,},
        .mouse_button = *mouse_button,
        .clicks = button.clicks,
      })
    ))
    .enqueue();

  return true;
}

bool TryPublishMouseMoveEvent(flecs::world world, const OgreBites::Event& ogre_event) {
  if (ogre_event.type != OgreBites::MOUSEMOTION) {
    return false;
  }

  const auto& motion = ogre_event.motion;

  // LOG_INFO("~~~ TryPublishMouseMoveEvent 1");

  world.event<input::SystemInputEvent>()
    .id<input::MousePos>()
    .entity(world.entity().set(
      input::MousePos {
        .x = motion.x,
        .y = motion.y,
      }
    ))
    .enqueue();

  world.event<input::SystemInputEvent>()
    .id<input::MouseMoveEvent>()
    .entity(world.entity().set(
      input::MouseMoveEvent {
        .delta = {
          .x = motion.xrel,
          .y = motion.yrel,
        },
      }
    ))
    .enqueue();

  // LOG_INFO("~~~ TryPublishMouseMoveEvent 2");

  return true;
}

template <typename KeyboardEvent, OgreBites::EventType Type>
bool TryPublishKeyboardEvent(flecs::world world, const OgreBites::Event& ogre_event) {
  if (ogre_event.type != Type) {
    return false;
  }

  const auto& key = ogre_event.key;

  world.event<input::SystemInputEvent>()
    .id<KeyboardEvent>()
    .entity(world.entity().set(
      KeyboardEvent ({
        .key = {
          .code = static_cast<decltype(input::Key::code)>(key.keysym.sym),
          .mod = static_cast<decltype(input::Key::mod)>(key.keysym.mod),
          .repeat = static_cast<decltype(input::Key::repeat)>(key.repeat),
        },
      })
    ))
    .enqueue();

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
