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

#include <cell_light/cell_light.h>

#include "cell_light_state.h"

namespace z13::station::light {

CellLight::CellLight() : state_(std::make_shared<State>()) {}

CellLight::CellLight(std::span<const z13::building::primitives::CellBox> opaque, std::span<const Lamp> lamps)
    : state_(std::make_shared<State>(opaque, lamps)) {}

LightChanges CellLight::Apply(const LightEdits& edits) {
  return state_->Apply(edits);
}

const LightLayout& CellLight::Layout() const {
  return state_->Nodes().Layout();
}

const std::vector<uint8_t>& CellLight::Labels() const {
  return state_->Nodes().Labels();
}

std::optional<size_t> CellLight::NodeAt(const Eigen::Vector3i& cell) const {
  return state_->Nodes().NodeAt(cell);
}

Eigen::Vector3f CellLight::LightAt(size_t node) const {
  return state_->LightAt(node);
}

Eigen::Vector3f CellLight::DirectionAt(size_t node) const {
  return state_->DirectionAt(node);
}

std::vector<Eigen::Vector4f> CellLight::Texels(const z13::building::primitives::CellBox& cubes, uint8_t piece,
                                               NodeTexel texel) const {
  return state_->Texels(cubes, piece, texel);
}

std::vector<Lamp> CellLight::Lamps() const {
  return state_->Lamps();
}

size_t CellLight::JoinedCubes() const {
  return state_->Nodes().JoinedCubes();
}

}  // namespace z13::station::light
