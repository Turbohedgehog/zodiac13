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

#include "station_editor_module.h"

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>

#include "editor_camera_system.h"
#include "editor_commands.h"
#include "editor_graph.h"
#include "editor_overlay_system.h"
#include "editor_selection_commands.h"
#include "editor_view_system.h"
#include "scene_reload_system.h"

namespace z13::station_editor {

StationEditorModule::StationEditorModule(flecs::world& world) {
  z13::OnRegisterComponents(world, [](flecs::world& w) {
    flecs_tools::RegisterComponent<EditorGraph>(w);
    RegisterEditorCommandComponents(w);
    RegisterEditorSelectionComponents(w);
  });
  z13::OnInitSystems(world, [](flecs::world& w) {
    w.set(EditorGraph {});
    RegisterEditorCommandSystems(w);
    RegisterEditorSelectionSystems(w);
  });
  EditorCameraSystem::Register(world);
  EditorOverlaySystem::Register(world);
  EditorViewSystem::Register(world);
  SceneReloadSystem::Register(world);
}

}  // namespace z13::station_editor
