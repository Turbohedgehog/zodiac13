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
#include <vector>

#include <Eigen/Dense>

#include <rooms/view_culling.h>

namespace z13::testing {

// A node of BoxTree over the items `first` to `first + count`; an inner node's left child follows
// it and its right child is `right` (a leaf has none).
struct BoxTreeNode {
  Eigen::AlignedBox3f bounds;
  uint32_t first {};
  uint32_t count {};
  uint32_t right {};
};

// A bounding volume hierarchy over boxes (median split on the longest axis of their centers).
// A prototype of the props' culling (docs/frame-time-plan.md): a query descends only into nodes
// whose box ViewCulling::Visible accepts, which never drops a visible item because a child lies
// inside its parent's box.
class BoxTree {
 public:
  explicit BoxTree(const std::vector<Eigen::AlignedBox3f>& boxes);

  // Appends the indices (into the constructor's boxes) of the visible boxes to `visible`. With
  // `fast_accept` a node wholly in view is taken without testing what it holds.
  void Query(
      const z13::station::rooms::ViewCulling& culling, std::vector<uint32_t>& visible, bool fast_accept = false) const;

  // Takes the boxes' new places (the same count, in the constructor's order) and refits the nodes
  // around them, keeping the tree's shape: the cheap update for boxes that moved a little.
  void Refit(const std::vector<Eigen::AlignedBox3f>& boxes);

  // The same for the boxes in `moved` alone (the rest stay where they were): only their leaves and
  // the nodes above them are recomputed.
  void Refit(const std::vector<uint32_t>& moved, const std::vector<Eigen::AlignedBox3f>& boxes);

  // The sum of the nodes' volumes: it grows as refitting loosens the tree, so a rebuild is due
  // when it is well above what a fresh tree has.
  double TotalVolume() const { return volume_; }

  size_t NodeCount() const { return nodes_.size(); }

 private:
  uint32_t Build(size_t begin, size_t end, uint32_t parent, const std::vector<Eigen::AlignedBox3f>& boxes);
  void RefitNode(size_t index);

  std::vector<Eigen::AlignedBox3f> items_;
  // The box index held in each item slot.
  std::vector<uint32_t> ids_;
  std::vector<Eigen::Vector3f> centers_;
  std::vector<BoxTreeNode> nodes_;
  std::vector<uint32_t> parents_;
  // The item slot of each box, and the leaf holding each slot.
  std::vector<uint32_t> slots_;
  std::vector<uint32_t> leaves_;
  std::vector<uint8_t> dirty_;
  double volume_ {};
};

}  // namespace z13::testing
