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

#include <lib_core/state/snapshot_grouping.h>

#include <numeric>

#include <lib_core/state/world_state.h>

namespace z13::flecs_tools {

namespace {

constexpr uint64_t kMixShiftFirst = 30;
constexpr uint64_t kMixShiftSecond = 27;
constexpr uint64_t kMixShiftLast = 31;
constexpr uint64_t kMixMultiplierFirst = 0xbf58476d1ce4e5b9ULL;
constexpr uint64_t kMixMultiplierSecond = 0x94d049bb133111ebULL;
constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

}  // namespace

uint64_t MixHash(uint64_t value) {
  value = (value ^ (value >> kMixShiftFirst)) * kMixMultiplierFirst;
  value = (value ^ (value >> kMixShiftSecond)) * kMixMultiplierSecond;
  return value ^ (value >> kMixShiftLast);
}

uint64_t HashName(std::string_view name) {
  return std::accumulate(name.begin(), name.end(), kFnvOffsetBasis, [](uint64_t hash, char c) {
    return (hash ^ static_cast<unsigned char>(c)) * kFnvPrime;
  });
}

void RegisterSnapshotGrouping(flecs::world& world) {
  RegisterComponent<SnapshotGrouping>(world);
  world.set<SnapshotGrouping>({});
}

}  // namespace z13::flecs_tools
