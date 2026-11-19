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
#include <lib_core/state/world_state.h>
#include <station_graph/graph_pick.h>
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
  const auto pick = graph::PickAlong(editor->get().graph(), editor->get().schema(), origin, forward);
  if (!pick) {
    reply.Fail(std::string(kNothingAimedAt));
    return;
  }
  Select(world, {.target = pick->kind == graph::PickKind::kRoom ? EditorTarget::kRoom : EditorTarget::kEdge, .id = pick->id},
         reply);
}

}  // namespace

void RegisterEditorSelectionComponents(flecs::world world) {
  flecs_tools::RegisterComponents<EditorSelectCommand, EditorPickCommand>(world);
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
}

}  // namespace z13::station_editor
