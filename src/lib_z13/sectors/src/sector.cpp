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

#include <sectors/sector.h>

#include <net_tuning_generated.h>

namespace z13::net::sectors {

int DefaultSectorCells() {
  return fbs::net::NetTuningT {}.sector_size_cells;
}

SectorKey SectorOf(const Eigen::Vector3i& cell, int sector_cells) {
  return z13::building::primitives::ChunkOf(cell, sector_cells);
}

std::vector<SectorKey> SectorsTouched(const z13::building::primitives::CellBox& box, int sector_cells) {
  std::vector<SectorKey> touched;
  if ((box.extent.array() <= 0).any()) {
    return touched;
  }
  const SectorKey first = SectorOf(box.min, sector_cells);
  const SectorKey last = SectorOf(box.End() - Eigen::Vector3i::Ones(), sector_cells);
  for (int x = first.x(); x <= last.x(); ++x) {
    for (int y = first.y(); y <= last.y(); ++y) {
      for (int z = first.z(); z <= last.z(); ++z) {
        touched.emplace_back(x, y, z);
      }
    }
  }
  return touched;
}

}  // namespace z13::net::sectors
