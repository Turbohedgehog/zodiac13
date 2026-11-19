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

#include "editor_commands.h"

#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/status.h>
#include <z13/components/editor/editor_view.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/station/station_mode.h>
#include <player/gameplay_entities.h>
#include <station_graph/graph_assets.h>
#include <station_graph/graph_diagnosis.h>
#include <station_graph/graph_editor.h>
#include <station_graph/graph_json.h>
#include <station_graph/property_schema.h>

#include "editor_graph.h"
#include "editor_tag.h"
#include "scene_reload.h"

namespace z13::station_editor {

namespace {

namespace graph = z13::editor::graph;

constexpr std::string_view kNodeTarget = "node";
constexpr std::string_view kEdgeTarget = "edge";
constexpr std::string_view kNoGraph = "no graph loaded: call editor_load first";

struct EditorLoadCommand {
  using Command = z13::Command<std::string,
                               "Loads the station graph `name` (assets/station/graph/<name>.json) and the property "
                               "schema, and starts its history. Answers the counts of rooms and edges.",
                               Tags<kDebugTag, kEditorTag>>;
  std::string name;
};

struct EditorShowCommand {
  using Command = z13::Command<std::string, "Answers the graph being edited as its file's JSON.",
                               Tags<kDebugTag, kEditorTag>>;
};

struct EditorSaveCommand {
  using Command = z13::Command<std::string,
                               "Writes the graph being edited to the file `path` (a path on this machine, not an "
                               "asset). The blueprint comes from running the pipeline on that file.",
                               Tags<kDebugTag, kEditorTag>>;
  std::string path;
};

struct EditorSetRoomCommand {
  using Command = z13::Command<std::string,
                               "Adds the room `id` with its box in cells, the wall ring included: lowest corner "
                               "(x, y, z), w by d across; one that exists is moved and resized instead. Rooms that "
                               "meet share one wall cell line.",
                               Tags<kDebugTag, kEditorTag>>;
  std::string id;
  int x {};
  int y {};
  int z {};
  int w {};
  int d {};
};

struct EditorRemoveRoomCommand {
  using Command = z13::Command<std::string, "Removes the room `id` and the edges that end in it.",
                               Tags<kDebugTag, kEditorTag>>;
  std::string id;
};

struct EditorAddEdgeCommand {
  using Command = z13::Command<std::string, "Adds the edge `id`: an opening between the rooms `from` and `to`.",
                               Tags<kDebugTag, kEditorTag>>;
  std::string id;
  std::string from;
  std::string to;
};

struct EditorRemoveEdgeCommand {
  using Command = z13::Command<std::string, "Removes the edge `id`.", Tags<kDebugTag, kEditorTag>>;
  std::string id;
};

struct EditorSetPropertyCommand {
  using Command = z13::Command<std::string,
                               "Sets the property `name` of the room or edge `id` (`target` is \"node\" or "
                               "\"edge\") to `value` (\"true\", \"14\", an enum's name); a value outside the schema is "
                               "refused with the allowed ones.",
                               Tags<kDebugTag, kEditorTag>>;
  std::string target;
  std::string id;
  std::string name;
  std::string value;
};

struct EditorReloadSceneCommand {
  using Command = z13::Command<std::string,
                               "Builds the station scene `scene` (assets/station/blueprints/<scene>.json) again from "
                               "its file, as the pipeline wrote it, and puts the local player back where it stood. "
                               "An empty `scene` takes the current one. Station mode only.",
                               Tags<kDebugTag, kEditorTag>>;
  std::string scene;
};

struct EditorWindowCommand {
  using Command = z13::Command<std::string, "Opens the editor's window, or closes it when it is open.",
                               Tags<kDebugTag, kEditorTag>>;
};

struct EditorUndoCommand {
  using Command = z13::Command<std::string, "Takes back the last edit.", Tags<kDebugTag, kEditorTag>>;
};

struct EditorRedoCommand {
  using Command = z13::Command<std::string, "Repeats the edit the last undo took back.",
                               Tags<kDebugTag, kEditorTag>>;
};

struct EditorDiagnoseCommand {
  using Command = z13::Command<std::vector<std::string>,
                               "Lists what keeps the graph from being built, one line each; empty when nothing does.",
                               Tags<kDebugTag, kEditorTag>>;
};

std::string SummaryOf(const graph::Graph& graph) {
  return std::format("{} rooms, {} edges", graph.nodes.size(), graph.edges.size());
}

std::expected<std::reference_wrapper<graph::GraphEditor>, std::string> LoadedEditor(flecs::world world) {
  auto& loaded = world.get_mut<EditorGraph>().editor;
  if (!loaded) {
    return std::unexpected(std::string(kNoGraph));
  }
  return std::ref(*loaded);
}

// Runs `edit` on the loaded editor and answers the graph's size, or the reason the edit was refused.
template <class Reply, class Edit>
void Edited(flecs::world world, Reply& reply, Edit&& edit) {
  const auto editor = LoadedEditor(world);
  if (!editor) {
    reply.Fail(editor.error());
    return;
  }
  if (const z13::Status done = edit(editor->get()); !done) {
    reply.Fail(done.error());
    return;
  }
  reply.Succeed(SummaryOf(editor->get().graph()));
}

void SetProperty(flecs::world world, const EditorSetPropertyCommand& request,
                 ReplyOf<EditorSetPropertyCommand>& reply) {
  Edited(world, reply, [&](graph::GraphEditor& editor) -> z13::Status {
    const bool node = request.target == kNodeTarget;
    if (!node && request.target != kEdgeTarget) {
      return std::unexpected(std::format("target must be '{}' or '{}'", kNodeTarget, kEdgeTarget));
    }
    const graph::PropertySpecs& specs = node ? editor.schema().node : editor.schema().edge;
    const auto spec = specs.find(request.name);
    if (spec == specs.end()) {
      return std::unexpected(std::format("unknown property '{}'", request.name));
    }
    const auto value = graph::ParsePropertyValue(spec->second, request.value);
    if (!value) {
      return std::unexpected(std::format("'{}' {}", request.name, value.error()));
    }
    return node ? editor.SetNodeProperty(request.id, request.name, *value)
                : editor.SetEdgeProperty(request.id, request.name, *value);
  });
}

z13::Status SetRoom(graph::GraphEditor& editor, const EditorSetRoomCommand& request) {
  const graph::RoomBox box {.origin = {request.x, request.y, request.z}, .size = {request.w, request.d}};
  if (graph::FindNode(editor.graph(), request.id)) {
    return editor.SetRoomBox(request.id, box);
  }
  return editor.AddRoom({.id = request.id, .box = box});
}

void Load(flecs::world world, const EditorLoadCommand& request, ReplyOf<EditorLoadCommand>& reply) {
  const z13::AssetSource& assets = world.get<z13::Assets>().source;
  auto schema = assets.ReadAsset(graph::PropertiesFile()).and_then([](const std::string& json) {
    return graph::ParsePropertySchema(json);
  });
  if (!schema) {
    reply.Fail(schema.error());
    return;
  }
  auto loaded = assets.ReadAsset(graph::GraphFile(request.name)).and_then([](const std::string& json) {
    return graph::ParseGraph(json);
  });
  if (!loaded) {
    reply.Fail(loaded.error());
    return;
  }
  const std::string summary = SummaryOf(*loaded);
  world.get_mut<EditorGraph>().editor.emplace(std::move(*loaded), std::move(*schema));
  reply.Succeed(summary);
}

void Save(flecs::world world, const EditorSaveCommand& request, ReplyOf<EditorSaveCommand>& reply) {
  const auto editor = LoadedEditor(world);
  if (!editor) {
    reply.Fail(editor.error());
    return;
  }
  const std::filesystem::path path = request.path;
  std::ofstream file(path, std::ios::binary);
  file << graph::WriteGraph(editor->get().graph());
  if (!file) {
    reply.Fail(std::format("cannot write {}", path.string()));
    return;
  }
  reply.Succeed(SummaryOf(editor->get().graph()));
}

void ReloadScene(flecs::world world, const EditorReloadSceneCommand& request, ReplyOf<EditorReloadSceneCommand>& reply) {
  if (!world.has<z13::station::StationMode>() || !world.has<z13::gameplay::Gameplay>()) {
    reply.Fail("not in a running station game: start a station scene first");
    return;
  }
  auto& choice = world.get_mut<z13::station::StationSceneChoice>();
  const std::string scene = request.scene.empty() ? choice.scene.value_or("") : request.scene;
  if (scene.empty()) {
    reply.Fail("no scene to reload: name one");
    return;
  }
  auto& reload = world.get_mut<SceneReload>();
  reload = {.pending = true};
  if (const auto* local = world.try_get<z13::gameplay::LocalPlayer>(); local != nullptr && local->id) {
    const flecs::entity player = world.lookup(z13::gameplay::PlayerEntityName(*local->id).c_str());
    if (player.is_valid() && player.has<Eigen::Matrix4f>()) {
      reload.pose = player.get<Eigen::Matrix4f>();
    }
  }
  choice.scene = scene;
  world.remove<z13::gameplay::Gameplay>();
  reply.Succeed(scene);
}

}  // namespace

void RegisterEditorCommandComponents(flecs::world world) {
  flecs_tools::RegisterComponents<EditorLoadCommand, EditorShowCommand, EditorSaveCommand, EditorSetRoomCommand,
                                  EditorRemoveRoomCommand, EditorAddEdgeCommand, EditorRemoveEdgeCommand,
                                  EditorSetPropertyCommand, EditorReloadSceneCommand, EditorWindowCommand,
                                  EditorUndoCommand,
                                  EditorRedoCommand, EditorDiagnoseCommand>(world);
}

void RegisterEditorCommandSystems(flecs::world world) {
  const auto writes = [](auto& builder) { builder.template write<EditorGraph>(); };
  CommandSystem<EditorLoadCommand>(world, "EditorCommands::Load", writes,
                                   [](flecs::iter& it, size_t, const EditorLoadCommand& request,
                                      ReplyOf<EditorLoadCommand>& reply) { Load(it.world(), request, reply); });
  CommandSystem<EditorShowCommand>(
      world, "EditorCommands::Show",
      [](flecs::iter& it, size_t, const EditorShowCommand&, ReplyOf<EditorShowCommand>& reply) {
        const auto editor = LoadedEditor(it.world());
        if (editor) {
          reply.Succeed(graph::WriteGraph(editor->get().graph()));
        } else {
          reply.Fail(editor.error());
        }
      });
  CommandSystem<EditorSaveCommand>(world, "EditorCommands::Save",
                                   [](flecs::iter& it, size_t, const EditorSaveCommand& request,
                                      ReplyOf<EditorSaveCommand>& reply) { Save(it.world(), request, reply); });
  CommandSystem<EditorSetRoomCommand>(
      world, "EditorCommands::SetRoom", writes,
      [](flecs::iter& it, size_t, const EditorSetRoomCommand& request, ReplyOf<EditorSetRoomCommand>& reply) {
        Edited(it.world(), reply, [&](graph::GraphEditor& editor) { return SetRoom(editor, request); });
      });
  CommandSystem<EditorRemoveRoomCommand>(
      world, "EditorCommands::RemoveRoom", writes,
      [](flecs::iter& it, size_t, const EditorRemoveRoomCommand& request, ReplyOf<EditorRemoveRoomCommand>& reply) {
        Edited(it.world(), reply, [&](graph::GraphEditor& editor) { return editor.RemoveNode(request.id); });
      });
  CommandSystem<EditorAddEdgeCommand>(
      world, "EditorCommands::AddEdge", writes,
      [](flecs::iter& it, size_t, const EditorAddEdgeCommand& request, ReplyOf<EditorAddEdgeCommand>& reply) {
        Edited(it.world(), reply, [&](graph::GraphEditor& editor) {
          return editor.AddEdge({.id = request.id, .from = request.from, .to = request.to});
        });
      });
  CommandSystem<EditorRemoveEdgeCommand>(
      world, "EditorCommands::RemoveEdge", writes,
      [](flecs::iter& it, size_t, const EditorRemoveEdgeCommand& request, ReplyOf<EditorRemoveEdgeCommand>& reply) {
        Edited(it.world(), reply, [&](graph::GraphEditor& editor) { return editor.RemoveEdge(request.id); });
      });
  CommandSystem<EditorSetPropertyCommand>(
      world, "EditorCommands::SetProperty", writes,
      [](flecs::iter& it, size_t, const EditorSetPropertyCommand& request, ReplyOf<EditorSetPropertyCommand>& reply) {
        SetProperty(it.world(), request, reply);
      });
  CommandSystem<EditorReloadSceneCommand>(
      world, "EditorCommands::ReloadScene",
      [](auto& builder) { builder.template write<SceneReload>().template write<z13::station::StationSceneChoice>(); },
      [](flecs::iter& it, size_t, const EditorReloadSceneCommand& request, ReplyOf<EditorReloadSceneCommand>& reply) {
        ReloadScene(it.world(), request, reply);
      });
  CommandSystem<EditorWindowCommand>(
      world, "EditorCommands::Window",
      [](flecs::iter& it, size_t, const EditorWindowCommand&, ReplyOf<EditorWindowCommand>& reply) {
        auto& view = it.world().get_mut<z13::editor::EditorView>();
        view.open = !view.open;
        reply.Succeed(view.open ? "the editor's window is open" : "the editor's window is closed");
      });
  CommandSystem<EditorUndoCommand>(
      world, "EditorCommands::Undo", writes,
      [](flecs::iter& it, size_t, const EditorUndoCommand&, ReplyOf<EditorUndoCommand>& reply) {
        Edited(it.world(), reply, [](graph::GraphEditor& editor) { return editor.Undo(); });
      });
  CommandSystem<EditorRedoCommand>(
      world, "EditorCommands::Redo", writes,
      [](flecs::iter& it, size_t, const EditorRedoCommand&, ReplyOf<EditorRedoCommand>& reply) {
        Edited(it.world(), reply, [](graph::GraphEditor& editor) { return editor.Redo(); });
      });
  CommandSystem<EditorDiagnoseCommand>(
      world, "EditorCommands::Diagnose",
      [](flecs::iter& it, size_t, const EditorDiagnoseCommand&, ReplyOf<EditorDiagnoseCommand>& reply) {
        const auto editor = LoadedEditor(it.world());
        if (editor) {
          reply.Succeed(graph::Diagnose(editor->get().graph(), editor->get().schema()));
        } else {
          reply.Fail(editor.error());
        }
      });
}

}  // namespace z13::station_editor
