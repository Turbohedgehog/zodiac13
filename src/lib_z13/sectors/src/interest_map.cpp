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

#include <sectors/interest_map.h>

#include <algorithm>

namespace z13::net::sectors {

namespace {

bool SectorLess(const SectorKey& a, const SectorKey& b) {
  return std::lexicographical_compare(a.data(), a.data() + a.size(), b.data(), b.data() + b.size());
}

}  // namespace

int SectorDistance(const SectorKey& a, const SectorKey& b) {
  return (a - b).cwiseAbs().maxCoeff();
}

InterestChange InterestMap::Update(ClientId client, const SectorKey& center, const InterestShape& shape) {
  auto [found, inserted] = zones_.try_emplace(client);
  Zone& zone = found->second;
  if (!inserted && zone.center == center && zone.shape == shape) {
    return {};
  }
  zone.center = center;
  zone.shape = shape;

  const int reach = shape.radius + shape.margin;
  InterestChange change;
  for (int x = -reach; x <= reach; ++x) {
    for (int y = -reach; y <= reach; ++y) {
      for (int z = -reach; z <= reach; ++z) {
        const SectorKey sector = center + SectorKey(x, y, z);
        if (zone.sectors.insert(sector).second) {
          change.entered.push_back(sector);
          subscribers_[sector].push_back(client);
        }
      }
    }
  }
  // Still held one sector past the reach: the hysteresis.
  std::erase_if(zone.sectors, [&](const SectorKey& sector) {
    if (SectorDistance(sector, center) <= reach + 1) {
      return false;
    }
    change.left.push_back(sector);
    auto& clients = subscribers_.find(sector)->second;
    std::erase(clients, client);
    if (clients.empty()) {
      subscribers_.erase(sector);
    }
    return true;
  });
  std::ranges::sort(change.entered, SectorLess);
  std::ranges::sort(change.left, SectorLess);
  return change;
}

void InterestMap::Remove(ClientId client) {
  const auto found = zones_.find(client);
  if (found == zones_.end()) {
    return;
  }
  for (const SectorKey& sector : found->second.sectors) {
    auto& clients = subscribers_.find(sector)->second;
    std::erase(clients, client);
    if (clients.empty()) {
      subscribers_.erase(sector);
    }
  }
  zones_.erase(found);
}

bool InterestMap::Covers(ClientId client, const SectorKey& sector) const {
  const auto found = zones_.find(client);
  return found != zones_.end() && found->second.sectors.contains(sector);
}

bool InterestMap::IsInner(ClientId client, const SectorKey& sector) const {
  const auto found = zones_.find(client);
  return found != zones_.end() && SectorDistance(sector, found->second.center) <= found->second.shape.radius;
}

std::span<const ClientId> InterestMap::SubscribersOf(const SectorKey& sector) const {
  const auto found = subscribers_.find(sector);
  return found == subscribers_.end() ? std::span<const ClientId> {} : std::span<const ClientId> {found->second};
}

std::vector<SectorKey> InterestMap::SectorsOf(ClientId client) const {
  const auto found = zones_.find(client);
  return found == zones_.end() ? std::vector<SectorKey> {}
                               : std::vector<SectorKey> {found->second.sectors.begin(), found->second.sectors.end()};
}

std::vector<ClientId> InterestMap::Clients() const {
  std::vector<ClientId> clients;
  clients.reserve(zones_.size());
  for (const auto& [client, zone] : zones_) {
    clients.push_back(client);
  }
  return clients;
}

size_t InterestMap::SectorCount(ClientId client) const {
  const auto found = zones_.find(client);
  return found == zones_.end() ? 0 : found->second.sectors.size();
}

}  // namespace z13::net::sectors
