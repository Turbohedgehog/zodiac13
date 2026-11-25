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

#include "editor_drag_system.h"

#include <optional>
#include <string>
#include <utility>

#include <flecs.h>

#include <editor_camera/orbit_camera.h>
#include <lib_core/commands/command_registry.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/status.h>
#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>
#include <station_graph/graph_pick.h>
#include <z13/components/editor/editor_camera.h>
#include <z13/components/editor/editor_drag.h>
#include <z13/components/editor/editor_view.h>
#include <z13/components/input/input_events.h>
#include <z13_settings/settings.h>

#include "editor_tag.h"
#include "editor_world.h"

namespace z13::station_editor {

namespace {

namespace graph = z13::editor::graph;
using z13::editor::EditorCamera;
using z13::editor::EditorDrag;
using z13::fbs::input::Keycode;

constexpr std::string_view kSetRoomName = "editor_set_room";

std::optional<Eigen::Vector2f> GroundPoint(const EditorCamera& view, const z13::EditorCameraTuning& tuning,
                                           const Eigen::Vector2f& pixel, float floor_cells) {
  const auto ray = PixelRay(view, tuning, pixel);
  const auto hit = ray ? z13::editor::camera::GroundHit(*ray, floor_cells) : std::nullopt;
  return hit ? std::optional<Eigen::Vector2f>(hit->head<2>()) : std::nullopt;
}

// The picked room, if the press landed on it: a drag of the selection from a press on empty floor would be a surprise.
const graph::Node* PressedRoom(flecs::world world, const EditorCamera& view, const z13::EditorCameraTuning& tuning,
                               const z13::editor::EditorView& editor_view, const Eigen::Vector2f& press) {
  const auto editor = LoadedEditor(world);
  const auto ray = PixelRay(view, tuning, press);
  if (!editor || !ray || !editor_view.selected || editor_view.selected->target != z13::editor::EditorTarget::kRoom) {
    return nullptr;
  }
  const auto pick = graph::PickAlong(editor->get().graph(), editor->get().schema(), ray->origin, ray->direction);
  const auto node = graph::FindNode(editor->get().graph(), editor_view.selected->id);
  const bool on_room = pick && pick->kind == graph::PickKind::kRoom && pick->id == editor_view.selected->id;
  return on_room && node ? &editor->get().graph().nodes[*node] : nullptr;
}

void OnDown(const z13::input::MouseButtonDownEvent& event, EditorDrag& drag, const EditorCamera& view) {
  if (view.active && event.button == Keycode::MOUSE_BUTTON_LEFT) {
    drag = EditorDrag {.pressed = true,
                       .press_pixel = Eigen::Vector2f(static_cast<float>(event.pos.x), static_cast<float>(event.pos.y))};
  }
}

void OnPointer(flecs::iter& it, size_t, const z13::input::MousePos& pointer, EditorDrag& drag, const EditorCamera& view,
               const z13::EditorCameraTuning& tuning, const z13::editor::EditorView& editor_view) {
  if (!drag.pressed || !view.active) {
    return;
  }
  const Eigen::Vector2f pixel(static_cast<float>(pointer.x), static_cast<float>(pointer.y));
  if (!drag.active) {
    const graph::Node* room = PressedRoom(it.world(), view, tuning, editor_view, drag.press_pixel);
    const auto grab = room ? GroundPoint(view, tuning, drag.press_pixel, static_cast<float>(room->box.origin.z()))
                           : std::nullopt;
    if (!grab) {
      return;
    }
    drag.active = true;
    drag.room = room->id;
    drag.grab_cells = *grab;
  }
  const auto editor = LoadedEditor(it.world());
  const auto node = editor ? graph::FindNode(editor->get().graph(), drag.room) : std::nullopt;
  if (!node) {
    return;
  }
  const auto now = GroundPoint(view, tuning, pixel, static_cast<float>(editor->get().graph().nodes[*node].box.origin.z()));
  if (now) {
    drag.offset_cells = (*now - drag.grab_cells).array().round().matrix().cast<int>();
  }
}

void OnUp(flecs::iter& it, size_t, const z13::input::MouseButtonUpEvent& event, EditorDrag& drag) {
  if (event.button != Keycode::MOUSE_BUTTON_LEFT) {
    return;
  }
  const EditorDrag finished = std::exchange(drag, EditorDrag {});
  const auto editor = LoadedEditor(it.world());
  const auto node = finished.active && editor ? graph::FindNode(editor->get().graph(), finished.room) : std::nullopt;
  if (!node || finished.offset_cells.isZero()) {
    return;
  }
  const graph::RoomBox& box = editor->get().graph().nodes[*node].box;
  rfl::Generic::Object arguments;
  arguments["id"] = rfl::Generic(finished.room);
  arguments["x"] = rfl::Generic(box.origin.x() + finished.offset_cells.x());
  arguments["y"] = rfl::Generic(box.origin.y() + finished.offset_cells.y());
  arguments["z"] = rfl::Generic(box.origin.z());
  arguments["w"] = rfl::Generic(box.size.x());
  arguments["d"] = rfl::Generic(box.size.y());
  const Status submitted = SubmitCommand(it.world(), {.tags = {std::string(z13::editor::kEditorTagName)}}, kSetRoomName,
                                         rfl::Generic(std::move(arguments)), [](CommandResult) {});
  if (!submitted) {
    log_error("{}", submitted.error());
  }
}

void RegisterSystems(flecs::world world) {
  world.observer<const z13::input::MouseButtonDownEvent, EditorDrag, const EditorCamera>("EditorDragSystem::OnDown")
      .event<z13::input::SystemInputEventType>()
      .each(OnDown);
  world.observer<const z13::input::MousePos, EditorDrag, const EditorCamera, const z13::EditorCameraTuning,
                 const z13::editor::EditorView>("EditorDragSystem::OnPointer")
      .event<z13::input::SystemInputEventType>()
      .each(OnPointer);
  world.observer<const z13::input::MouseButtonUpEvent, EditorDrag>("EditorDragSystem::OnUp")
      .event<z13::input::SystemInputEventType>()
      .each(OnUp);
}

}  // namespace

void EditorDragSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, [](flecs::world& w) { flecs_tools::RegisterComponent<z13::editor::EditorDrag>(w); });
  z13::OnInitSystems(world, [](flecs::world& w) {
    w.set(EditorDrag {});
    RegisterSystems(w);
  });
}

}  // namespace z13::station_editor
