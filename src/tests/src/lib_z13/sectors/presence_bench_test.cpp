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

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <random>
#include <unordered_set>
#include <vector>

#include <sectors/sector.h>
#include <sectors/sector_index.h>

// Ways to find the players in a client's presence after it crosses a sector border, to see which
// wins at what player count (docs/interest-design.md). DISABLED: run with --gtest_also_run_disabled_tests
// --gtest_filter='PresenceBench.*'. Debug build, like the rest of our measuring.
namespace z13::net::sectors {
namespace {

using SectorSet = std::unordered_set<SectorKey, SectorHash>;

constexpr std::array kPlayerCounts = {50, 200, 1000, 5000};
// The station's footprint in sectors and the sectors a client's presence reaches from its own.
constexpr int kStationSectors = 13;
constexpr int kDeckSectors = 2;
constexpr int kPresenceReach = 3;
constexpr int kRepeats = 2000;
constexpr uint32_t kSeed = 2026;

SectorSet Cube(const SectorKey& center, int reach) {
  SectorSet set;
  for (int dx = -reach; dx <= reach; ++dx) {
    for (int dy = -reach; dy <= reach; ++dy) {
      for (int dz = -reach; dz <= reach; ++dz) {
        set.insert(center + SectorKey(dx, dy, dz));
      }
    }
  }
  return set;
}

int Distance(const SectorKey& a, const SectorKey& b) {
  return (a - b).cwiseAbs().maxCoeff();
}

template <typename Work>
double MicrosecondsPer(Work&& work) {
  const auto start = std::chrono::steady_clock::now();
  for (int repeat = 0; repeat < kRepeats; ++repeat) {
    work();
  }
  return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / kRepeats;
}

TEST(PresenceBench, DISABLED_PlayersInAClientsPresence) {
  std::cout << "players | set built | list + set | list + box | sectors + MembersIn | border faces only\n";
  for (const int players : kPlayerCounts) {
    std::mt19937 random(kSeed);
    std::uniform_int_distribution<int> across(-kStationSectors / 2, kStationSectors / 2);
    std::uniform_int_distribution<int> deck(0, kDeckSectors - 1);
    SectorIndex index;
    for (int id = 0; id < players; ++id) {
      index.Place({MemberKind::kPlayer, static_cast<uint64_t>(id)},
                  SectorKey(across(random), across(random), deck(random)));
    }
    const SectorKey before(0, 0, 0);
    const SectorKey after(1, 0, 0);
    const SectorSet presence = Cube(after, kPresenceReach);
    size_t sink = 0;

    const double built = MicrosecondsPer([&] { sink += Cube(after, kPresenceReach).size(); });
    const double list_set = MicrosecondsPer([&] {
      for (const uint64_t id : index.IdsOf(MemberKind::kPlayer)) {
        const auto sector = index.Locate({MemberKind::kPlayer, id});
        sink += sector && presence.contains(*sector);
      }
    });
    const double list_box = MicrosecondsPer([&] {
      for (const uint64_t id : index.IdsOf(MemberKind::kPlayer)) {
        const auto sector = index.Locate({MemberKind::kPlayer, id});
        sink += sector && Distance(*sector, after) <= kPresenceReach;
      }
    });
    const double sectors = MicrosecondsPer([&] {
      for (const SectorKey& sector : presence) {
        sink += index.MembersIn(sector).size();
      }
    });
    // The sectors the presence gained or lost moving one sector along x: two faces.
    const double faces = MicrosecondsPer([&] {
      for (int dy = -kPresenceReach; dy <= kPresenceReach; ++dy) {
        for (int dz = -kPresenceReach; dz <= kPresenceReach; ++dz) {
          sink += index.MembersIn(before + SectorKey(-kPresenceReach, dy, dz)).size();
          sink += index.MembersIn(after + SectorKey(kPresenceReach, dy, dz)).size();
        }
      }
    });
    std::cout << std::format("{:7} | {:7.1f} us | {:8.1f} us | {:8.1f} us | {:15.1f} us | {:12.1f} us  (sink {})\n",
                             players, built, list_set, list_box, sectors, faces, sink);
  }
}

}  // namespace
}  // namespace z13::net::sectors
