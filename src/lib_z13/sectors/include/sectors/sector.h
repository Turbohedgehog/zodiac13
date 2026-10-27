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

#include <vector>

#include <Eigen/Core>

#include <primitives/placement.h>

namespace z13::net::sectors {

// A sector is a cube of `sector_cells`³ cells; its key is the cube's coordinate, by floor
// division like a chunk's.
using SectorKey = Eigen::Vector3i;
using SectorHash = z13::building::primitives::CellHash;

// The sectors from `min` to `max`, both included, along every axis.
struct SectorBox {
  SectorKey min = SectorKey::Zero();
  SectorKey max = SectorKey::Zero();

  bool Contains(const SectorKey& sector) const {
    return (sector.array() >= min.array()).all() && (sector.array() <= max.array()).all();
  }
  SectorBox Grown(int by) const { return {min - SectorKey::Constant(by), max + SectorKey::Constant(by)}; }
  // Every sector of the box, for a caller that has to visit them.
  std::vector<SectorKey> Sectors() const;
};

// net_tuning.fbs's default, read in sector.cpp so this header doesn't include the schema.
int DefaultSectorCells();

SectorKey SectorOf(const Eigen::Vector3i& cell, int sector_cells);

// Every sector the box touches; none for an empty box.
std::vector<SectorKey> SectorsTouched(const z13::building::primitives::CellBox& box, int sector_cells);

}  // namespace z13::net::sectors
