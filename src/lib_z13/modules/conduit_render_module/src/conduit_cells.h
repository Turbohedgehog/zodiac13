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

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>

#include <primitives/placement.h>
#include <z13/components/conduit.h>

namespace z13::conduit_render {

// Which runs fill which cells, for the corners where a run meets one going another way. Derived, never state.
class ConduitCells {
 public:
  using Singleton = void;

  // Takes `conduits` in a fixed order; false if they are those it holds already.
  bool Rebuild(std::vector<z13::conduit::Conduit> conduits, const z13::conduit::ConduitKinds& kinds);
  // A run of `network` fills `cell` along another axis than `axis`.
  bool Turns(std::string_view network, const Eigen::Vector3i& cell, z13::conduit::ConduitAxis axis) const;

 private:
  struct Occupant {
    std::string network;
    z13::conduit::ConduitAxis axis {};
  };

  std::vector<z13::conduit::Conduit> built_;
  std::unordered_map<Eigen::Vector3i, std::vector<Occupant>, z13::building::primitives::CellHash> by_cell_;
};

}  // namespace z13::conduit_render
