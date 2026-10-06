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

#include <z13_primitives/placement.h>

#include <algorithm>
#include <array>

namespace z13::primitives {

namespace {

constexpr int kAxes = 3;
constexpr int kSignCombinations = 8;

// Row-major signed permutation matrices.
using IntMatrix = std::array<int8_t, kAxes * kAxes>;

constexpr int Determinant(const IntMatrix& m) {
  return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
         m[2] * (m[3] * m[7] - m[4] * m[6]);
}

// Every signed permutation with determinant +1, permutations in lexicographic order and
// then sign patterns, so the identity comes first and the numbering never changes.
consteval std::array<IntMatrix, kOrientationCount> MakeRotations() {
  std::array<IntMatrix, kOrientationCount> rotations {};
  std::array<int, kAxes> permutation {0, 1, 2};
  size_t count = 0;
  do {
    for (int signs = 0; signs < kSignCombinations; ++signs) {
      IntMatrix m {};
      for (int row = 0; row < kAxes; ++row) {
        m[row * kAxes + permutation[row]] = static_cast<int8_t>((signs >> row) & 1 ? -1 : 1);
      }
      if (Determinant(m) == 1) {
        rotations[count++] = m;
      }
    }
  } while (std::next_permutation(permutation.begin(), permutation.end()));
  return rotations;
}

constexpr std::array<IntMatrix, kOrientationCount> kRotations = MakeRotations();

}  // namespace

Eigen::Matrix3i OrientationMatrix(uint8_t orientation) {
  const IntMatrix& m = kRotations[orientation];
  Eigen::Matrix3i matrix;
  for (int row = 0; row < kAxes; ++row) {
    for (int column = 0; column < kAxes; ++column) {
      matrix(row, column) = m[row * kAxes + column];
    }
  }
  return matrix;
}

uint8_t InverseOrientation(uint8_t orientation) {
  const Eigen::Matrix3i inverse = OrientationMatrix(orientation).transpose();
  for (uint8_t candidate = 0; candidate < kOrientationCount; ++candidate) {
    if (OrientationMatrix(candidate) == inverse) {
      return candidate;
    }
  }
  return 0;
}

bool CellBox::Contains(const Eigen::Vector3i& cell) const {
  return (cell.array() >= min.array()).all() && (cell.array() < End().array()).all();
}

bool CellBox::Overlaps(const CellBox& other) const {
  return (min.array() < other.End().array()).all() && (other.min.array() < End().array()).all();
}

CellBox Placement::Occupied() const {
  return {.min = cell, .extent = OrientationMatrix(orientation).cwiseAbs() * size};
}

// The rotated frame spans [rotation * 0, rotation * size] per axis in some order; shifting
// its lowest corner onto `cell` gives the origin.
CellPose Placement::Pose() const {
  const Eigen::Matrix3i rotation = OrientationMatrix(orientation);
  const Eigen::Vector3i rotated_size = rotation * size;
  return {.rotation = rotation, .origin = cell - rotated_size.cwiseMin(Eigen::Vector3i::Zero())};
}

Placement PlaceCentredOn(const Eigen::Vector3f& point, const Eigen::Vector3i& size, uint8_t orientation) {
  const Eigen::Vector3i extent = OrientationMatrix(orientation).cwiseAbs() * size;
  const Eigen::Vector3f lowest = point - extent.cast<float>() / 2.f;
  const Eigen::Vector3i cell = lowest.array().round().cast<int>();
  return {.cell = cell, .size = size, .orientation = orientation};
}

Eigen::Isometry3f WorldPose(const CellPose& pose, float cell_size) {
  Eigen::Isometry3f world = Eigen::Isometry3f::Identity();
  world.linear() = pose.rotation.cast<float>();
  world.translation() = pose.origin.cast<float>() * cell_size;
  return world;
}

}  // namespace z13::primitives
