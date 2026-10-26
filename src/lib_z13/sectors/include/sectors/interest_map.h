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

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sectors/sector.h>

namespace z13::net::sectors {

// A client is a player's id.
using ClientId = uint32_t;

// How far a client's interest reaches from its sector, in sectors: the inner ring is shown and
// checked, the margin around it is only simulated.
struct InterestShape {
  int radius {};
  int margin {};
  bool operator==(const InterestShape&) const = default;
};

// The sectors a client entered and left in one update, in sector order.
struct InterestChange {
  std::vector<SectorKey> entered;
  std::vector<SectorKey> left;
};

// Largest difference along an axis between two sectors.
int SectorDistance(const SectorKey& a, const SectorKey& b);

// The sectors each client is subscribed to, and the clients each sector has. A client enters a
// sector when it is within radius + margin of the client's own, and keeps it until it is one
// sector further, so a player on a border does not flicker in and out.
class InterestMap {
 public:
  using Singleton = void;
  using SessionScoped = void;

  // Moves the client's interest to `center`; does nothing when neither it nor the shape changed.
  InterestChange Update(ClientId client, const SectorKey& center, const InterestShape& shape);
  void Remove(ClientId client);

  bool Covers(ClientId client, const SectorKey& sector) const;
  // Within the inner ring: shown to the client, not only simulated for it.
  bool IsInner(ClientId client, const SectorKey& sector) const;
  // In no particular order.
  std::span<const ClientId> SubscribersOf(const SectorKey& sector) const;
  std::vector<ClientId> Clients() const;
  size_t SectorCount(ClientId client) const;

 private:
  struct Zone {
    SectorKey center = SectorKey::Zero();
    InterestShape shape;
    std::unordered_set<SectorKey, SectorHash> sectors;
  };

  std::unordered_map<ClientId, Zone> zones_;
  std::unordered_map<SectorKey, std::vector<ClientId>, SectorHash> subscribers_;
};

}  // namespace z13::net::sectors
