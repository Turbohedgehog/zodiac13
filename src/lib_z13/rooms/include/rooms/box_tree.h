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
#include <vector>

#include <Eigen/Dense>

namespace z13::station::rooms {

class ViewCulling;

// A node of BoxTree over the items `first` to `first + count`; an inner node's left child follows
// it and its right child is `right` (a leaf has none).
struct BoxTreeNode {
  Eigen::AlignedBox3f bounds;
  uint32_t first {};
  uint32_t count {};
  uint32_t right {};
};

// A bounding volume hierarchy over boxes (median split on the longest axis of their centers), so
// that the props in view are found without testing every prop (docs/frame-time-plan.md). A box is
// known by its index in the span the tree was built from.
class BoxTree {
 public:
  BoxTree() = default;
  explicit BoxTree(std::span<const Eigen::AlignedBox3f> boxes);

  // Appends the indices of the boxes that `culling` shows, in no particular order. A node is
  // skipped when it is not visible and taken whole when all of it is.
  void Query(const ViewCulling& culling, std::vector<uint32_t>& visible) const;

  // The boxes at `moved` are now where `boxes` has them (all the boxes, as built): refits the
  // nodes around them, and rebuilds the tree when that has made it loose. A different number of
  // boxes rebuilds it too.
  void Update(std::span<const uint32_t> moved, std::span<const Eigen::AlignedBox3f> boxes);

  size_t Size() const { return items_.size(); }
  size_t Rebuilds() const { return rebuilds_; }

 private:
  uint32_t Build(size_t begin, size_t end, uint32_t parent, std::span<const Eigen::AlignedBox3f> boxes);
  void BuildAll(std::span<const Eigen::AlignedBox3f> boxes);
  void RefitNode(size_t index);

  std::vector<Eigen::AlignedBox3f> items_;
  // The box index held in each item slot, and the slot of each box.
  std::vector<uint32_t> ids_;
  std::vector<uint32_t> slots_;
  std::vector<Eigen::Vector3f> centers_;
  std::vector<BoxTreeNode> nodes_;
  std::vector<uint32_t> parents_;
  // The leaf holding each slot.
  std::vector<uint32_t> leaves_;
  std::vector<uint8_t> dirty_;
  // The sum of the nodes' volumes, and what it was when the tree was built.
  double volume_ {};
  double built_volume_ {};
  size_t rebuilds_ {};
};

}  // namespace z13::station::rooms
