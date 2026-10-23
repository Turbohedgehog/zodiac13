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

#include <rooms/room_visibility.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>

#include <lib_core/utils/frustum.h>

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;

constexpr size_t kBoxCorners = 8;
// Points are kept this far in front of the eye's plane, where projecting still works.
constexpr float kMinDepth = 1e-4f;
// A clip-space point's x, y, z and w; a matrix of those is column-major.
constexpr size_t kClipComponents = 4;
constexpr size_t kSpaceAxes = 3;
constexpr size_t kWComponent = 3;

// A point in clip space, and the corners of a box in it. Plain C arrays, not Eigen or std::array: a
// Debug build makes the per-portal math several times as slow with them (see Frustum::Intersects).
struct ClipPoint {
  float v[kClipComponents];
};

struct ClipBox {
  ClipPoint corner[kBoxCorners];
};

// The screen box that points added to it reach.
class ScreenBox {
 public:
  void Add(float x, float y) {
    low_x_ = std::min(low_x_, x);
    low_y_ = std::min(low_y_, y);
    high_x_ = std::max(high_x_, x);
    high_y_ = std::max(high_y_, y);
  }
  void Add(const ClipPoint& clip) { Add(clip.v[0] / clip.v[kWComponent], clip.v[1] / clip.v[kWComponent]); }
  bool Empty() const { return low_x_ > high_x_ || low_y_ > high_y_; }
  // The part of `seen` this box covers; nothing where they share no point.
  std::optional<Eigen::AlignedBox2f> Within(const Eigen::AlignedBox2f& seen) const {
    const float* seen_low = seen.min().data();
    const float* seen_high = seen.max().data();
    const float low_x = std::max(low_x_, seen_low[0]);
    const float low_y = std::max(low_y_, seen_low[1]);
    const float high_x = std::min(high_x_, seen_high[0]);
    const float high_y = std::min(high_y_, seen_high[1]);
    if (low_x > high_x || low_y > high_y) {
      return std::nullopt;
    }
    return Eigen::AlignedBox2f(Eigen::Vector2f(low_x, low_y), Eigen::Vector2f(high_x, high_y));
  }

 private:
  float low_x_ = std::numeric_limits<float>::max();
  float low_y_ = std::numeric_limits<float>::max();
  float high_x_ = std::numeric_limits<float>::lowest();
  float high_y_ = std::numeric_limits<float>::lowest();
};

// The corners of `opening` in clip space, one bit per axis set for the high side (as Eigen numbers
// them).
ClipBox ClipCorners(const CellBox& opening, const Eigen::Matrix4f& view_projection) {
  const float* matrix = view_projection.data();
  float low[kSpaceAxes];
  float high[kSpaceAxes];
  for (size_t axis = 0; axis < kSpaceAxes; ++axis) {
    low[axis] = static_cast<float>(opening.min[axis]);
    high[axis] = static_cast<float>(opening.End()[axis]);
  }
  ClipBox clip;
  for (size_t corner = 0; corner < kBoxCorners; ++corner) {
    const float point[kSpaceAxes] = {(corner & 1) != 0 ? high[0] : low[0], (corner & 2) != 0 ? high[1] : low[1],
                                     (corner & 4) != 0 ? high[2] : low[2]};
    for (size_t row = 0; row < kClipComponents; ++row) {
      clip.corner[corner].v[row] = matrix[row + (kClipComponents * kSpaceAxes)] + (matrix[row] * point[0]) +
                                   (matrix[row + kClipComponents] * point[1]) +
                                   (matrix[row + (2 * kClipComponents)] * point[2]);
    }
  }
  return clip;
}

// The part of `seen` the opening covers: its corners in front of the eye, and where its
// edges cross into the eye's plane, so an opening beside the eye is not taken whole.
std::optional<Eigen::AlignedBox2f> Through(
    const Eigen::AlignedBox2f& seen, const CellBox& opening, const Eigen::Matrix4f& view_projection) {
  const ClipBox clip = ClipCorners(opening, view_projection);
  ScreenBox covered;
  for (size_t i = 0; i < kBoxCorners; ++i) {
    const ClipPoint& from = clip.corner[i];
    const bool front = from.v[kWComponent] > kMinDepth;
    if (front) {
      covered.Add(from);
    }
    for (size_t axis = 0; axis < kSpaceAxes; ++axis) {
      const size_t j = i | (size_t {1} << axis);
      const ClipPoint& to = clip.corner[j];
      if (j != i && front != (to.v[kWComponent] > kMinDepth)) {
        const float t = (kMinDepth - from.v[kWComponent]) / (to.v[kWComponent] - from.v[kWComponent]);
        ClipPoint crossing;
        for (size_t row = 0; row < kClipComponents; ++row) {
          crossing.v[row] = from.v[row] + (t * (to.v[row] - from.v[row]));
        }
        covered.Add(crossing);
      }
    }
  }
  if (covered.Empty()) {
    return std::nullopt;
  }
  return covered.Within(seen);
}

// Whether the eye, looking from `from`, sees the opening's face toward `from` rather than
// its back: it is not past the opening's far face.
bool FacesEye(const Portal& portal, RoomId from, const Eigen::Vector3f& eye) {
  const auto axis = static_cast<Eigen::Index>(portal.axis);
  const float along = eye[axis];
  if (from == portal.low_side) {
    return along < static_cast<float>(portal.opening.End()[axis]);
  }
  return along > static_cast<float>(portal.opening.min[axis]);
}

std::array<float, kSpaceAxes> CornerOf(const CellBox& box, bool high) {
  std::array<float, kSpaceAxes> corner {};
  for (size_t axis = 0; axis < kSpaceAxes; ++axis) {
    corner[axis] = static_cast<float>(high ? box.End()[axis] : box.min[axis]);
  }
  return corner;
}

bool TooSmall(const Eigen::AlignedBox2f& region, const Eigen::Vector2f& min_extent) {
  const float* low = region.min().data();
  const float* high = region.max().data();
  return high[0] - low[0] < min_extent.x() || high[1] - low[1] < min_extent.y();
}

bool Contains(const Eigen::AlignedBox2f& outer, const Eigen::AlignedBox2f& inner) {
  const float* outer_low = outer.min().data();
  const float* outer_high = outer.max().data();
  const float* inner_low = inner.min().data();
  const float* inner_high = inner.max().data();
  return outer_low[0] <= inner_low[0] && outer_low[1] <= inner_low[1] && outer_high[0] >= inner_high[0] &&
         outer_high[1] >= inner_high[1];
}

}  // namespace

// A room reached again through another portal grows its region and is walked again;
// regions only grow, from a finite set of edges, so the walk ends.
ScreenRegions VisibleRooms(const RoomGraph& graph, const RoomView& view) {
  ScreenRegions seen;
  if (!graph.rooms.contains(view.eye_room)) {
    return seen;
  }
  seen.emplace(view.eye_room, view.screen);
  const z13::math::Frustum sides(view.view_projection);
  std::vector<RoomId> pending {view.eye_room};
  while (!pending.empty()) {
    const RoomId room = pending.back();
    pending.pop_back();
    const Eigen::AlignedBox2f room_region = seen.at(room);
    for (const size_t index : graph.PortalsOf(room)) {
      const Portal& portal = graph.portals[index];
      if (!portal.visible || portal.a == portal.b || !FacesEye(portal, room, view.eye)) {
        continue;
      }
      if (!sides.IntersectsSides(CornerOf(portal.opening, false), CornerOf(portal.opening, true))) {
        continue;
      }
      const RoomId other = portal.a == room ? portal.b : portal.a;
      const auto through = Through(room_region, portal.opening, view.view_projection);
      if (!through || TooSmall(*through, view.min_extent)) {
        continue;
      }
      const auto [region, added] = seen.try_emplace(other, *through);
      if (!added) {
        if (Contains(region->second, *through)) {
          continue;
        }
        region->second.extend(*through);
      }
      pending.push_back(other);
    }
  }
  return seen;
}

}  // namespace z13::station::rooms
