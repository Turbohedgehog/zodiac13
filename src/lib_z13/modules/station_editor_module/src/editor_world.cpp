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

#include "editor_world.h"

#include <z13/components/gameplay/player.h>
#include <player/gameplay_entities.h>

#include "editor_graph.h"

namespace z13::station_editor {

std::expected<std::reference_wrapper<z13::editor::graph::GraphEditor>, std::string> LoadedEditor(flecs::world world) {
  auto& loaded = world.get_mut<EditorGraph>().editor;
  if (!loaded) {
    return std::unexpected(std::string(kNoGraph));
  }
  return std::ref(*loaded);
}

std::optional<Eigen::Matrix4f> LocalPlayerPose(flecs::world world) {
  const auto* local = world.try_get<z13::gameplay::LocalPlayer>();
  if (local == nullptr || !local->id) {
    return std::nullopt;
  }
  const flecs::entity player = world.lookup(z13::gameplay::PlayerEntityName(*local->id).c_str());
  if (!player.is_valid() || !player.has<Eigen::Matrix4f>()) {
    return std::nullopt;
  }
  return player.get<Eigen::Matrix4f>();
}

}  // namespace z13::station_editor
