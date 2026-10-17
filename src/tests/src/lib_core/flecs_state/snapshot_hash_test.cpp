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

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <lib_core/state/snapshot_hash.h>

namespace z13::flecs_tools {
namespace {

WorldSnapshot MakeSnapshot() {
  WorldSnapshot snapshot;
  snapshot.entities.push_back({
      .name = "Player_1",
      .tags = {"z13::Tag"},
      .components = {{.type = "z13::Flashlight", .value = {1}}},
      .relationships = {{.relation = "ChildOf", .target = "Station"}},
  });
  snapshot.groups.push_back(std::make_shared<const GroupSnapshot>(GroupSnapshot {.id = 7, .fingerprint = 42}));
  return snapshot;
}

TEST(SnapshotHashTest, EqualSnapshotsHashEqually) {
  EXPECT_EQ(HashSnapshot(MakeSnapshot()), HashSnapshot(MakeSnapshot()));
}

TEST(SnapshotHashTest, EveryPartChangesTheHash) {
  const uint64_t original = HashSnapshot(MakeSnapshot());
  const std::vector<std::function<void(WorldSnapshot&)>> changes = {
      [](WorldSnapshot& s) { s.entities[0].name = "Player_2"; },
      [](WorldSnapshot& s) { s.entities[0].tags.clear(); },
      [](WorldSnapshot& s) { s.entities[0].components[0].value[0] = 0; },
      [](WorldSnapshot& s) { s.entities[0].relationships[0].target = "Ship"; },
      [](WorldSnapshot& s) {
        s.groups[0] = std::make_shared<const GroupSnapshot>(GroupSnapshot {.id = 7, .fingerprint = 43});
      },
      // Bytes moved between neighboring fields.
      [](WorldSnapshot& s) {
        s.entities[0].components[0].type = "z13::Flashligh";
        s.entities[0].components[0].value = {'t', 1};
      },
  };
  for (const auto& change : changes) {
    WorldSnapshot changed = MakeSnapshot();
    change(changed);
    EXPECT_NE(HashSnapshot(changed), original);
  }
}

}  // namespace
}  // namespace z13::flecs_tools
