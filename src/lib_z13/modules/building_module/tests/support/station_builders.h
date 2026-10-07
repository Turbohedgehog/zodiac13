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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/utils/math.h>
#include <z13/components/building.h>
#include <z13/components/station.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include "../../../z13_module/tests/support/z13_test_world.h"

namespace z13::testing {

inline std::vector<std::string> SiteArgs() {
  return {std::string(kStationSceneArg), std::string(kSiteScene)};
}

inline std::vector<std::string> WithServerArg(std::vector<std::string> args) {
  args.emplace_back(kServerArg);
  return args;
}

inline Z13TestWorld StationWorld() {return Z13TestWorld(SiteArgs());
}

inline Eigen::Matrix4f Facing(const Eigen::Vector3f& position, const Eigen::Vector3f& forward) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 3>(0, 0) =
      Eigen::Quaternionf::FromTwoVectors(Eigen::Vector3f::UnitX(), forward.normalized()).toRotationMatrix();
  z13::math::SetTranslation(position, transform);
  return transform;
}

// An extra player driven by its transform and request tags, so it can look anywhere
// without mouse input.
inline flecs::entity AddBuilder(Z13TestWorld& test_world, uint32_t id, const Eigen::Matrix4f& transform) {
  flecs::entity builder = z13::gameplay::SpawnPlayer(test_world.World(), id).set(transform);
  builder.add<z13::building::BuildingTool>();
  // The brush and the BlockBrush appear on the first frame.
  test_world.Tick();
  return builder;
}

inline std::vector<z13::station::Block> BlocksOfType(flecs::world world, uint32_t type_id) {
  std::vector<z13::station::Block> blocks;
  world.query_builder<const z13::station::Block>().build().each([&](const z13::station::Block& block) {
    if (block.spec.type_id == type_id) {
      blocks.push_back(block);
    }
  });
  return blocks;
}

}  // namespace z13::testing
