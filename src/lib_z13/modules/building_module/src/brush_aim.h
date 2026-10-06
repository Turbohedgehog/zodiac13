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

#include <Eigen/Dense>

#include <z13/components/station.h>
#include <z13_grid/block_index.h>

namespace z13::building {

// Where a brush points, in cells: the face of the first block along its ray (`normal` is
// non-zero), else the free point at its reach, `eye` being where the ray started.
struct Aim {
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  Eigen::Vector3f point = Eigen::Vector3f::Zero();
  Eigen::Vector3i normal = Eigen::Vector3i::Zero();

  // The cell a build here starts from: the empty one in front of the face.
  Eigen::Vector3i Cell() const;
  // The same aim from the other side of the face: the cell behind it, and a block placed
  // into the face rather than against it.
  Aim Flipped() const;
};

// Aims the ray from `eye` to `reach` at `index`.
Aim AimAt(const grid::BlockIndex& index, const Eigen::Vector3f& eye, const Eigen::Vector3f& reach);

// The block of `spec` resting on the aimed face, slid along it off what it would sink into,
// or centred on the free point and then drawn back along the ray until it clears others.
// With `may_overlap` (a cut takes the cells first) it stays where aimed.
z13::station::Block BlockAt(
    const Aim& aim, const z13::station::BlockSpec& spec, const grid::BlockIndex& index, bool may_overlap = false);

}  // namespace z13::building
