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

#include "editor_selection_commands.h"

#include <expected>
#include <format>
#include <string>
#include <string_view>

#include <flecs.h>

#include <lib_core/commands/command_registry.h>
#include <editor_camera/orbit_camera.h>
#include <lib_core/state/world_state.h>
#include <z13/components/input/input_events.h>
#include <z13_settings/settings.h>
#include <station_graph/graph_pick.h>
#include <z13/components/editor/editor_camera.h>
#include <z13/components/editor/editor_view.h>
#include <z13/components/station/block_spec.h>

#include "editor_tag.h"
#include "editor_world.h"

namespace z13::station_editor {

namespace {

namespace graph = z13::editor::graph;
using z13::editor::EditorSelection;
using z13::editor::EditorTarget;

constexpr std::string_view kNodeTarget = "node";
constexpr std::string_view kEdgeTarget = "edge";
constexpr std::string_view kNothingAimedAt = "nothing is aimed at";
constexpr std::string_view kNoPlayer = "the local player is not in the world";
constexpr std::string_view kCameraOff = "the orbit camera is off (editor_camera) or has not drawn yet";
constexpr std::string_view kPickAtName = "editor_pick_at";
constexpr float kNdcSpan = 2.f;  // the screen runs from -1 to 1 along each axis
// The player's forward axis in its pose (the building module aims along the same column).
constexpr int kForwardColumn = 0;

struct EditorSelectCommand {
  using Command = z13::Command<std::string,
                               "Picks the room or edge `id` (`target` is \"node\" or \"edge\"): the window shows its "
                               "properties and the graph layer highlights it.",
                               Tags<kDebugTag, kEditorTag>>;
  std::string target;
  std::string id;
};

struct EditorPickCommand {
  using Command = z13::Command<std::string,
                               "Picks the room, or the opening, the local player's view points at. A ray that "
                               "meets nothing picks nothing.",
                               Tags<kDebugTag, kEditorTag>>;
};

struct EditorPickAtCommand {
  using Command = z13::Command<std::string,
                               "Picks the room, or the opening, under the pixel (x, y) of the window, seen from the "
                               "orbit camera. A ray that meets nothing picks nothing.",
                               Tags<kDebugTag, kEditorTag>>;
  int x {};
  int y {};
};

bool Exists(const graph::Graph& graph, EditorTarget target, std::string_view id) {
  return (target == EditorTarget::kRoom ? graph::FindNode(graph, id) : graph::FindEdge(graph, id)).has_value();
}

std::string Describe(const EditorSelection& selection) {
  return std::format("{} {}", selection.target == EditorTarget::kRoom ? "room" : "edge", selection.id);
}

template <class Reply>
void Select(flecs::world world, const EditorSelection& selection, Reply& reply) {
  const auto editor = LoadedEditor(world);
  if (!editor) {
    reply.Fail(editor.error());
    return;
  }
  if (!Exists(editor->get().graph(), selection.target, selection.id)) {
    reply.Fail(std::format("there is no {}", Describe(selection)));
    return;
  }
  world.get_mut<z13::editor::EditorView>().selected = selection;
  reply.Succeed(Describe(selection));
}

void SelectByName(flecs::world world, const EditorSelectCommand& request, ReplyOf<EditorSelectCommand>& reply) {
  if (request.target != kNodeTarget && request.target != kEdgeTarget) {
    reply.Fail(std::format("target must be '{}' or '{}'", kNodeTarget, kEdgeTarget));
    return;
  }
  Select(world, {.target = request.target == kNodeTarget ? EditorTarget::kRoom : EditorTarget::kEdge, .id = request.id},
         reply);
}

template <class Reply>
void PickAlongRay(flecs::world world, const graph::GraphEditor& editor, const Eigen::Vector3f& origin,
                  const Eigen::Vector3f& direction, Reply& reply) {
  const auto pick = graph::PickAlong(editor.graph(), editor.schema(), origin, direction);
  if (!pick) {
    reply.Fail(std::string(kNothingAimedAt));
    return;
  }
  Select(world, {.target = pick->kind == graph::PickKind::kRoom ? EditorTarget::kRoom : EditorTarget::kEdge, .id = pick->id},
         reply);
}

void PickAimed(flecs::world world, ReplyOf<EditorPickCommand>& reply) {
  const auto editor = LoadedEditor(world);
  if (!editor) {
    reply.Fail(editor.error());
    return;
  }
  const auto pose = LocalPlayerPose(world);
  if (!pose) {
    reply.Fail(std::string(kNoPlayer));
    return;
  }
  const Eigen::Vector3f origin = pose->block<3, 1>(0, 3) / z13::station::kCellSize;
  const Eigen::Vector3f forward = pose->block<3, 3>(0, 0).col(kForwardColumn);
  PickAlongRay(world, editor->get(), origin, forward, reply);
}

void PickAtPixel(flecs::world world, const EditorPickAtCommand& request, ReplyOf<EditorPickAtCommand>& reply) {
  const auto editor = LoadedEditor(world);
  if (!editor) {
    reply.Fail(editor.error());
    return;
  }
  const auto& view = world.get<z13::editor::EditorCamera>();
  if (!view.active || view.screen_pixels.minCoeff() <= 0.f) {
    reply.Fail(std::string(kCameraOff));
    return;
  }
  const Eigen::Vector2f pixel(static_cast<float>(request.x), static_cast<float>(request.y));
  const Eigen::Vector2f ndc(kNdcSpan * pixel.x() / view.screen_pixels.x() - 1.f,
                            1.f - kNdcSpan * pixel.y() / view.screen_pixels.y());
  const auto& tuning = world.get<z13::EditorCameraTuning>();
  const auto ray = z13::editor::camera::RayThrough(view.orbit, ndc, tuning.fov_degrees,
                                                  view.screen_pixels.x() / view.screen_pixels.y());
  PickAlongRay(world, editor->get(), ray.origin, ray.direction, reply);
}

// A left click while the camera is on picks what is under it; a miss leaves the selection as it was.
void OnClick(flecs::iter& it, size_t, const z13::input::MouseButtonDownEvent& event, const z13::editor::EditorCamera& view) {
  if (!view.active || event.button != z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT) {
    return;
  }
  rfl::Generic::Object arguments;
  arguments["x"] = rfl::Generic(event.pos.x);
  arguments["y"] = rfl::Generic(event.pos.y);
  SubmitCommand(it.world(), {.tags = {std::string(z13::editor::kEditorTagName)}}, kPickAtName,
                rfl::Generic(std::move(arguments)), [](CommandResult) {});
}

}  // namespace

void RegisterEditorSelectionComponents(flecs::world world) {
  flecs_tools::RegisterComponents<EditorSelectCommand, EditorPickCommand, EditorPickAtCommand>(world);
}

void RegisterEditorSelectionSystems(flecs::world world) {
  const auto writes = [](auto& builder) { builder.template write<z13::editor::EditorView>(); };
  CommandSystem<EditorSelectCommand>(
      world, "EditorCommands::Select", writes,
      [](flecs::iter& it, size_t, const EditorSelectCommand& request, ReplyOf<EditorSelectCommand>& reply) {
        SelectByName(it.world(), request, reply);
      });
  CommandSystem<EditorPickCommand>(
      world, "EditorCommands::Pick", writes,
      [](flecs::iter& it, size_t, const EditorPickCommand&, ReplyOf<EditorPickCommand>& reply) {
        PickAimed(it.world(), reply);
      });
  CommandSystem<EditorPickAtCommand>(
      world, "EditorCommands::PickAt", writes,
      [](flecs::iter& it, size_t, const EditorPickAtCommand& request, ReplyOf<EditorPickAtCommand>& reply) {
        PickAtPixel(it.world(), request, reply);
      });
  world.observer<const z13::input::MouseButtonDownEvent, const z13::editor::EditorCamera>("EditorSelection::OnClick")
      .event<z13::input::SystemInputEventType>()
      .each(OnClick);
}

}  // namespace z13::station_editor
