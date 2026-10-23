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

#include <rooms/box_tree.h>

#include <algorithm>
#include <numeric>

#include <rooms/view_culling.h>

namespace z13::station::rooms {
namespace {

// Items in a leaf: small enough to test one by one, large enough to keep the tree shallow.
constexpr size_t kLeafItems = 8;

// Plain floats, not Eigen: this runs on every node a refit touches, and a Debug build keeps every
// Eigen temporary.
double VolumeOf(const Eigen::AlignedBox3f& box) {
  const float* low = box.min().data();
  const float* high = box.max().data();
  return static_cast<double>(high[0] - low[0]) * static_cast<double>(high[1] - low[1]) *
         static_cast<double>(high[2] - low[2]);
}

}  // namespace

BoxTree::BoxTree(std::span<const Eigen::AlignedBox3f> boxes, double loose_ratio) : loose_ratio_(loose_ratio) {
  BuildAll(boxes);
}

void BoxTree::BuildAll(std::span<const Eigen::AlignedBox3f> boxes) {
  const double loose_ratio = loose_ratio_;
  *this = BoxTree();
  loose_ratio_ = loose_ratio;
  ids_.resize(boxes.size());
  std::iota(ids_.begin(), ids_.end(), 0U);
  centers_.reserve(boxes.size());
  for (const Eigen::AlignedBox3f& box : boxes) {
    centers_.push_back(box.center());
  }
  leaves_.resize(boxes.size());
  if (!boxes.empty()) {
    nodes_.reserve((boxes.size() / kLeafItems * 2) + 1);
    Build(0, boxes.size(), 0, boxes);
  }
  items_.reserve(boxes.size());
  slots_.resize(boxes.size());
  for (size_t slot = 0; slot < ids_.size(); ++slot) {
    items_.push_back(boxes[ids_[slot]]);
    slots_[ids_[slot]] = static_cast<uint32_t>(slot);
  }
  dirty_.assign(nodes_.size(), 0);
  volume_ = std::accumulate(nodes_.begin(), nodes_.end(), 0.0, [](double sum, const BoxTreeNode& node) {
    return sum + VolumeOf(node.bounds);
  });
  built_volume_ = volume_;
  centers_ = {};
}

uint32_t BoxTree::Build(size_t begin, size_t end, uint32_t parent, std::span<const Eigen::AlignedBox3f> boxes) {
  const auto index = static_cast<uint32_t>(nodes_.size());
  nodes_.push_back({.first = static_cast<uint32_t>(begin), .count = static_cast<uint32_t>(end - begin)});
  parents_.push_back(parent);
  Eigen::AlignedBox3f centers;
  for (size_t i = begin; i < end; ++i) {
    nodes_[index].bounds.extend(boxes[ids_[i]]);
    centers.extend(centers_[ids_[i]]);
  }
  if (end - begin <= kLeafItems) {
    std::fill(leaves_.begin() + static_cast<std::ptrdiff_t>(begin), leaves_.begin() + static_cast<std::ptrdiff_t>(end), index);
    return index;
  }
  Eigen::Index axis = 0;
  centers.sizes().maxCoeff(&axis);
  const auto middle = static_cast<std::ptrdiff_t>(begin + ((end - begin) / 2));
  std::nth_element(ids_.begin() + static_cast<std::ptrdiff_t>(begin), ids_.begin() + middle,
                   ids_.begin() + static_cast<std::ptrdiff_t>(end),
                   [&](uint32_t a, uint32_t b) { return centers_[a][axis] < centers_[b][axis]; });
  Build(begin, static_cast<size_t>(middle), index, boxes);
  nodes_[index].right = Build(static_cast<size_t>(middle), end, index, boxes);
  return index;
}

void BoxTree::RefitNode(size_t index) {
  BoxTreeNode& node = nodes_[index];
  volume_ -= VolumeOf(node.bounds);
  node.bounds = Eigen::AlignedBox3f();
  if (node.right == 0) {
    for (uint32_t slot = node.first; slot < node.first + node.count; ++slot) {
      node.bounds.extend(items_[slot]);
    }
  } else {
    node.bounds.extend(nodes_[index + 1].bounds);
    node.bounds.extend(nodes_[node.right].bounds);
  }
  volume_ += VolumeOf(node.bounds);
}

void BoxTree::Update(std::span<const uint32_t> moved, std::span<const Eigen::AlignedBox3f> boxes) {
  if (boxes.size() != items_.size()) {
    BuildAll(boxes);
    return;
  }
  for (const uint32_t id : moved) {
    const uint32_t slot = slots_[id];
    items_[slot] = boxes[id];
    dirty_[leaves_[slot]] = 1;
  }
  // A child always comes after its parent, so one pass backwards refits each dirty node after its
  // children and marks the parent.
  for (size_t i = nodes_.size(); i-- > 0;) {
    if (dirty_[i] == 0) {
      continue;
    }
    dirty_[i] = 0;
    RefitNode(i);
    if (i != 0) {
      dirty_[parents_[i]] = 1;
    }
  }
  if (volume_ > loose_ratio_ * built_volume_) {
    const size_t rebuilds = rebuilds_ + 1;
    BuildAll(boxes);
    rebuilds_ = rebuilds;
  }
}

void BoxTree::Query(const ViewCulling& culling, std::vector<uint32_t>& visible) const {
  if (nodes_.empty()) {
    return;
  }
  std::vector<uint32_t> pending {0};
  while (!pending.empty()) {
    const uint32_t index = pending.back();
    const BoxTreeNode& node = nodes_[index];
    pending.pop_back();
    if (!culling.Visible(node.bounds)) {
      continue;
    }
    if (culling.AllVisible(node.bounds)) {
      visible.insert(visible.end(), ids_.begin() + node.first, ids_.begin() + node.first + node.count);
      continue;
    }
    if (node.right != 0) {
      pending.push_back(node.right);
      pending.push_back(index + 1);
      continue;
    }
    for (uint32_t slot = node.first; slot < node.first + node.count; ++slot) {
      if (culling.Visible(items_[slot])) {
        visible.push_back(ids_[slot]);
      }
    }
  }
}

}  // namespace z13::station::rooms
