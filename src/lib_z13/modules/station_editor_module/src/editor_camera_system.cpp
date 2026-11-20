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

#include "editor_camera_system.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <flecs.h>

#include <editor_camera/orbit_camera.h>
#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>
#include <station_graph/graph_geometry.h>
#include <z13/components/editor/editor_camera.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/input/input_events.h>
#include <z13_settings/settings.h>

#include "editor_tag.h"
#include "editor_world.h"

namespace z13::station_editor {

namespace {

namespace camera = z13::editor::camera;
using z13::editor::EditorCamera;
using z13::editor::EditorFinger;
using z13::editor::OrbitCamera;
using z13::fbs::input::Keycode;

constexpr std::size_t kPinchFingers = 2;
// Before a graph is framed: the origin from about a hall's distance, tipped to look from above.
OrbitCamera StartView() {
  return {.distance = 80.f, .yaw_degrees = 35.f, .pitch_degrees = 55.f};
}

struct EditorCameraCommand {
  using Command = z13::Command<std::string,
                               "Switches the editor's orbit camera on, framing the graph, or off when it is on. "
                               "Right drag pans, middle drag orbits, the wheel zooms; two fingers pan, pinch and turn.",
                               Tags<kDebugTag, kEditorTag>>;
};

camera::DistanceLimits LimitsOf(const z13::EditorCameraTuning& tuning) {
  return {.min = tuning.min_distance_cells, .max = tuning.max_distance_cells};
}

// The loaded graph in view, or `from` when there is none.
OrbitCamera Framing(flecs::world world, const OrbitCamera& from, const z13::EditorCameraTuning& tuning) {
  const auto editor = LoadedEditor(world);
  if (!editor || editor->get().graph().nodes.empty()) {
    return from;
  }
  const auto& graph = editor->get().graph();
  Eigen::Vector3i low = Eigen::Vector3i::Constant(std::numeric_limits<int>::max());
  Eigen::Vector3i high = Eigen::Vector3i::Constant(std::numeric_limits<int>::lowest());
  for (const auto& node : graph.nodes) {
    const auto cells = z13::editor::graph::CellsOfRoom(node, editor->get().schema());
    low = low.cwiseMin(cells.min);
    high = high.cwiseMax(cells.min + cells.extent);
  }
  return camera::Framed(from, low.cast<float>(), (high - low).cast<float>(), tuning.fov_degrees, LimitsOf(tuning));
}

void Toggle(flecs::world world, ReplyOf<EditorCameraCommand>& reply) {
  auto& state = world.get_mut<EditorCamera>();
  const auto& tuning = world.get<z13::EditorCameraTuning>();
  const bool on = !state.active;
  state = EditorCamera {.active = on, .orbit = on ? Framing(world, state.orbit, tuning) : state.orbit};
  if (on) {
    world.add<z13::gameplay::FreeCursor>();
  } else {
    world.remove<z13::gameplay::FreeCursor>();
  }
  reply.Succeed(on ? "the orbit camera is on" : "the orbit camera is off");
}

void OnMouseDown(const z13::input::MouseButtonDownEvent& event, EditorCamera& state,
                 const z13::EditorCameraTuning& tuning) {
  if (!state.active) {
    return;
  }
  switch (event.button) {
    case Keycode::MOUSE_BUTTON_RIGHT:
      state.pan_held = true;
      break;
    case Keycode::MOUSE_BUTTON_MIDDLE:
      state.orbit_held = true;
      break;
    case Keycode::MOUSE_WHEEL_UP:
      state.orbit = camera::Zoomed(state.orbit, 1.f / tuning.zoom_per_notch, LimitsOf(tuning));
      break;
    case Keycode::MOUSE_WHEEL_DOWN:
      state.orbit = camera::Zoomed(state.orbit, tuning.zoom_per_notch, LimitsOf(tuning));
      break;
    default:
      break;
  }
}

void OnMouseUp(const z13::input::MouseButtonUpEvent& event, EditorCamera& state) {
  if (event.button == Keycode::MOUSE_BUTTON_RIGHT) {
    state.pan_held = false;
  } else if (event.button == Keycode::MOUSE_BUTTON_MIDDLE) {
    state.orbit_held = false;
  }
}

void OnMouseMove(const z13::input::MouseMoveEvent& event, EditorCamera& state, const z13::EditorCameraTuning& tuning) {
  const Eigen::Vector2f drag(static_cast<float>(event.delta.x), static_cast<float>(event.delta.y));
  if (state.active && state.pan_held) {
    state.orbit = camera::Panned(state.orbit, drag, tuning.pan_per_pixel);
  }
  if (state.active && state.orbit_held) {
    state.orbit = camera::Orbited(state.orbit, drag, tuning.orbit_degrees_per_pixel);
  }
}

void OnTouchDown(const z13::input::TouchDownEvent& event, EditorCamera& state) {
  if (state.active) {
    state.fingers.push_back({.finger = event.finger, .position = event.position});
  }
}

void OnTouchMove(const z13::input::TouchMoveEvent& event, EditorCamera& state, const z13::EditorCameraTuning& tuning) {
  const auto moved = std::ranges::find(state.fingers, event.finger, &EditorFinger::finger);
  if (moved == state.fingers.end()) {
    return;
  }
  const std::vector<EditorFinger> before = state.fingers;
  moved->position = event.position;
  if (state.fingers.size() != kPinchFingers) {
    return;
  }
  const camera::Gesture gesture = camera::TwoFingerGesture(before[0].position, before[1].position,
                                                           state.fingers[0].position, state.fingers[1].position);
  state.orbit = camera::Panned(state.orbit, gesture.pan_pixels, tuning.pan_per_pixel);
  state.orbit = camera::Zoomed(state.orbit, gesture.zoom_factor, LimitsOf(tuning));
  state.orbit = camera::Turned(state.orbit, gesture.twist_degrees);
}

void OnTouchUp(const z13::input::TouchUpEvent& event, EditorCamera& state) {
  std::erase_if(state.fingers, [&event](const EditorFinger& finger) { return finger.finger == event.finger; });
}

void RegisterSystems(flecs::world world) {
  world.observer<const z13::input::MouseButtonDownEvent, EditorCamera, const z13::EditorCameraTuning>(
           "EditorCameraSystem::OnMouseDown")
      .event<z13::input::SystemInputEventType>()
      .each(OnMouseDown);
  world.observer<const z13::input::MouseButtonUpEvent, EditorCamera>("EditorCameraSystem::OnMouseUp")
      .event<z13::input::SystemInputEventType>()
      .each(OnMouseUp);
  world.observer<const z13::input::MouseMoveEvent, EditorCamera, const z13::EditorCameraTuning>(
           "EditorCameraSystem::OnMouseMove")
      .event<z13::input::SystemInputEventType>()
      .each(OnMouseMove);
  world.observer<const z13::input::TouchDownEvent, EditorCamera>("EditorCameraSystem::OnTouchDown")
      .event<z13::input::SystemInputEventType>()
      .each(OnTouchDown);
  world.observer<const z13::input::TouchMoveEvent, EditorCamera, const z13::EditorCameraTuning>(
           "EditorCameraSystem::OnTouchMove")
      .event<z13::input::SystemInputEventType>()
      .each(OnTouchMove);
  world.observer<const z13::input::TouchUpEvent, EditorCamera>("EditorCameraSystem::OnTouchUp")
      .event<z13::input::SystemInputEventType>()
      .each(OnTouchUp);

  CommandSystem<EditorCameraCommand>(
      world, "EditorCommands::Camera",
      [](auto& builder) { builder.template write<EditorCamera>().template write<z13::gameplay::FreeCursor>(); },
      [](flecs::iter& it, size_t, const EditorCameraCommand&, ReplyOf<EditorCameraCommand>& reply) {
        Toggle(it.world(), reply);
      });
}

}  // namespace

void EditorCameraSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, [](flecs::world& w) {
    flecs_tools::RegisterComponent<EditorCamera>(w);
    flecs_tools::RegisterComponent<z13::EditorCameraTuning>(w);
    flecs_tools::RegisterComponent<EditorCameraCommand>(w);
  });
  z13::OnInitSystems(world, [](flecs::world& w) {
    w.set(EditorCamera {.orbit = StartView()});
    w.add<z13::EditorCameraTuning>();
    RegisterSystems(w);
  });
}

}  // namespace z13::station_editor
