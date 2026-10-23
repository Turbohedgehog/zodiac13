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

#include <z13_tests/box_tree.h>

#include <algorithm>
#include <numeric>

namespace z13::testing {
namespace {

// Items in a leaf: small enough to test one by one, large enough to keep the tree shallow.
constexpr size_t kLeafItems = 8;

// Plain floats, not Eigen: a Debug build keeps every Eigen temporary and this runs on each node.
double VolumeOf(const Eigen::AlignedBox3f& box) {
  const float* low = box.min().data();
  const float* high = box.max().data();
  return static_cast<double>(high[0] - low[0]) * static_cast<double>(high[1] - low[1]) *
         static_cast<double>(high[2] - low[2]);
}

}  // namespace

BoxTree::BoxTree(const std::vector<Eigen::AlignedBox3f>& boxes) : ids_(boxes.size()) {
  std::iota(ids_.begin(), ids_.end(), 0U);
  centers_.reserve(boxes.size());
  for (const Eigen::AlignedBox3f& box : boxes) {
    centers_.push_back(box.center());
  }
  if (!boxes.empty()) {
    nodes_.reserve(boxes.size() / kLeafItems * 2 + 1);
    Build(0, boxes.size(), 0, boxes);
  }
  slots_.resize(boxes.size());
  for (size_t slot = 0; slot < ids_.size(); ++slot) {
    slots_[ids_[slot]] = static_cast<uint32_t>(slot);
  }
  dirty_.assign(nodes_.size(), 0);
  for (const BoxTreeNode& node : nodes_) {
    volume_ += VolumeOf(node.bounds);
  }
  items_.reserve(boxes.size());
  for (const uint32_t id : ids_) {
    items_.push_back(boxes[id]);
  }
  centers_.clear();
  centers_.shrink_to_fit();
}

uint32_t BoxTree::Build(size_t begin, size_t end, uint32_t parent, const std::vector<Eigen::AlignedBox3f>& boxes) {
  const auto index = static_cast<uint32_t>(nodes_.size());
  nodes_.emplace_back();
  parents_.push_back(parent);
  Eigen::AlignedBox3f bounds;
  Eigen::AlignedBox3f centers;
  for (size_t i = begin; i < end; ++i) {
    bounds.extend(boxes[ids_[i]]);
    centers.extend(centers_[ids_[i]]);
  }
  nodes_[index].bounds = bounds;
  nodes_[index].first = static_cast<uint32_t>(begin);
  nodes_[index].count = static_cast<uint32_t>(end - begin);
  if (end - begin <= kLeafItems) {
    leaves_.resize(std::max(leaves_.size(), end), 0);
    std::fill(leaves_.begin() + static_cast<std::ptrdiff_t>(begin), leaves_.begin() + static_cast<std::ptrdiff_t>(end), index);
    return index;
  }
  Eigen::Index axis = 0;
  centers.sizes().maxCoeff(&axis);
  const size_t middle = begin + (end - begin) / 2;
  std::nth_element(ids_.begin() + static_cast<std::ptrdiff_t>(begin), ids_.begin() + static_cast<std::ptrdiff_t>(middle),
                   ids_.begin() + static_cast<std::ptrdiff_t>(end),
                   [&](uint32_t a, uint32_t b) { return centers_[a][axis] < centers_[b][axis]; });
  Build(begin, middle, index, boxes);
  nodes_[index].right = Build(middle, end, index, boxes);
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

void BoxTree::Refit(const std::vector<Eigen::AlignedBox3f>& boxes) {
  for (size_t slot = 0; slot < items_.size(); ++slot) {
    items_[slot] = boxes[ids_[slot]];
  }
  // A child always comes after its parent, so one pass backwards refits every node after its children.
  for (size_t i = nodes_.size(); i-- > 0;) {
    RefitNode(i);
  }
}

void BoxTree::Refit(const std::vector<uint32_t>& moved, const std::vector<Eigen::AlignedBox3f>& boxes) {
  for (const uint32_t id : moved) {
    const uint32_t slot = slots_[id];
    items_[slot] = boxes[id];
    dirty_[leaves_[slot]] = 1;
  }
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
}

void BoxTree::Query(
    const z13::station::rooms::ViewCulling& culling, std::vector<uint32_t>& visible, bool fast_accept) const {
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
    if (fast_accept && culling.AllVisible(node.bounds)) {
      visible.insert(visible.end(), ids_.begin() + node.first, ids_.begin() + node.first + node.count);
      continue;
    }
    if (node.right != 0) {
      pending.push_back(node.right);
      pending.push_back(index + 1);
      continue;
    }
    for (uint32_t i = node.first; i < node.first + node.count; ++i) {
      if (culling.Visible(items_[i])) {
        visible.push_back(ids_[i]);
      }
    }
  }
}

}  // namespace z13::testing
