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

#include "scene_overlay.h"

#include <array>
#include <span>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <Eigen/Dense>

#include <lib_core/utils/math.h>
#include <z13/components/station/overlay_layers.h>
#include <z13/components/station/station_mode.h>
#include <primitives/placement.h>
#include <primitives/chunk_mesh.h>

#include "../tools/math_convert.h"
#include "block_checker.h"

namespace z13::raylib {

namespace {

constexpr ::Color kBrushPreviewTint {255, 255, 255, 128};
constexpr ::Color kValidBuildTint {120, 255, 120, 128};
constexpr ::Color kRefusedBuildTint {255, 90, 90, 128};
// A cut that would be accepted: the box it takes, in orange.
constexpr ::Color kValidCutTint {255, 170, 60, 128};

void DrawBlock(const SingleBlockDrawing& drawing, const z13::station::Block& block, ::Color tint) {
  const std::shared_ptr<::Model> model =
      drawing.meshes.get().Get({.type_id = block.spec.type_id, .size = block.spec.size}, drawing.palette);
  if (model->meshCount == 0) {
    return;
  }
  UseChecker(drawing.lighting, z13::building::primitives::MaterialOf(drawing.palette, block.spec.type_id));
  const Eigen::Isometry3f pose =
      z13::building::primitives::WorldPose(z13::building::primitives::PoseOf(block), z13::station::kCellSize);
  model->transform = EigenToRaylibMatrix(Eigen::Matrix4f(pose.matrix()));
  DrawModel(*model, Vector3Zero(), 1.f, tint);
}

// A box corner's index has one bit per axis (x, y, z), set for the high side of the axis.
constexpr std::array kAxisBits {1, 2, 4};
constexpr int kBoxCorners = 1 << kAxisBits.size();

// Plain floats, not Eigen: a Debug build keeps every Eigen temporary, and a station has thousands of boxes.
struct Corner {
  float x {};
  float y {};
  float z {};
};

// The 12 edges of a box in world coordinates, straight into the line batch: no matrix
// stack, whose state this renderer sets by hand (BeginScene3D).
void AddBoxEdges(const Corner& low, const Corner& high) {
  const auto corner = [&](int index) {
    return Corner {(index & kAxisBits[0]) != 0 ? high.x : low.x, (index & kAxisBits[1]) != 0 ? high.y : low.y,
                   (index & kAxisBits[2]) != 0 ? high.z : low.z};
  };
  // Corners differing in exactly one bit are joined.
  for (int index = 0; index < kBoxCorners; ++index) {
    for (const int bit : kAxisBits) {
      if ((index & bit) == 0) {
        const Corner from = corner(index);
        const Corner to = corner(index | bit);
        rlVertex3f(from.x, from.y, from.z);
        rlVertex3f(to.x, to.y, to.z);
      }
    }
  }
}

// The debug overlay's boxes as wireframes, when it is on: on the default shader and texture
// (the block shader would light the lines by view angle) and past the depth test, as the
// edges lie on the walls' own faces.
void DrawOverlayLines(std::span<const z13::station::OverlayBox> boxes,
                      std::span<const z13::station::OverlaySegment> segments) {
  rlDrawRenderBatchActive();
  rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault());
  rlSetTexture(rlGetTextureIdDefault());
  rlDisableDepthTest();
  rlBegin(RL_LINES);
  constexpr float kCell = z13::station::kCellSize;
  for (const z13::station::OverlayBox& box : boxes) {
    rlColor4ub(box.color[0], box.color[1], box.color[2], box.color[3]);
    const Corner low {static_cast<float>(box.min.x()) * kCell, static_cast<float>(box.min.y()) * kCell,
                      static_cast<float>(box.min.z()) * kCell};
    const Corner high {static_cast<float>(box.min.x() + box.extent.x()) * kCell,
                       static_cast<float>(box.min.y() + box.extent.y()) * kCell,
                       static_cast<float>(box.min.z() + box.extent.z()) * kCell};
    AddBoxEdges(low, high);
  }
  for (const z13::station::OverlaySegment& segment : segments) {
    rlColor4ub(segment.color[0], segment.color[1], segment.color[2], segment.color[3]);
    rlVertex3f(segment.a.x() * kCell, segment.a.y() * kCell, segment.a.z() * kCell);
    rlVertex3f(segment.b.x() * kCell, segment.b.y() * kCell, segment.b.z() * kCell);
  }
  rlEnd();
  rlDrawRenderBatchActive();
  rlEnableDepthTest();
}

}  // namespace

void DrawBrushPreview(
    const flecs::world& world, flecs::entity brush, const Eigen::Matrix4f& transform,
    const SingleBlockDrawing& drawing) {
  if (world.has<z13::station::StationMode>()) {
    if (brush.has<z13::station::BrushPreview>()) {
      const auto& preview = brush.get<z13::station::BrushPreview>();
      const ::Color valid_tint =
          preview.kind == z13::station::BrushPreview::Kind::kBuild ? kValidBuildTint : kValidCutTint;
      const ::Color tint = preview.valid ? valid_tint : kRefusedBuildTint;
      DrawBlock(drawing, preview.block, tint);
      for (const z13::station::Block& part : preview.parts) {
        DrawBlock(drawing, part, tint);
      }
    }
    return;
  }
  const Eigen::Vector3f point = z13::math::ExtractTranslation<float>(transform) / z13::station::kCellSize;
  DrawBlock(drawing, z13::building::primitives::PlaceCentredOn(point, z13::station::CubeSpec()), kBrushPreviewTint);
}

void DrawRoomOverlay(const flecs::world& world) {
  const auto* overlay = world.try_get<z13::station::RoomOverlay>();
  if (overlay == nullptr || overlay->mode == z13::station::RoomOverlayMode::kOff) {
    return;
  }
  DrawOverlayLines(overlay->boxes, overlay->segments);
}

// The layer a module publishes (z13/components/station/overlay_layers.h), if one is shown.
void DrawLayerOverlay(const flecs::world& world) {
  const auto* layers = world.try_get<z13::station::OverlayLayers>();
  const z13::station::OverlayLayer* layer = layers != nullptr && layers->shown ? layers->Find(*layers->shown) : nullptr;
  if (layer != nullptr) {
    DrawOverlayLines(layer->boxes, {});
  }
}

// On the default shader and past the depth test, like the room overlay.
void DrawBrushLines(const BrushQuery& brushes) {
  rlDrawRenderBatchActive();
  rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault());
  rlSetTexture(rlGetTextureIdDefault());
  rlDisableDepthTest();
  rlBegin(RL_LINES);
  brushes.each([](flecs::entity brush, const z13::building::Brush&, const Eigen::Matrix4f&) {
    if (!brush.has<z13::station::BrushPreview>()) {
      return;
    }
    const auto& preview = brush.get<z13::station::BrushPreview>();
    const ::Color tint = preview.valid ? kValidBuildTint : kRefusedBuildTint;
    rlColor4ub(tint.r, tint.g, tint.b, 255);
    for (const z13::station::BrushLine& line : preview.lines) {
      const Eigen::Vector3f from = line.from * z13::station::kCellSize;
      const Eigen::Vector3f to = line.to * z13::station::kCellSize;
      rlVertex3f(from.x(), from.y(), from.z());
      rlVertex3f(to.x(), to.y(), to.z());
    }
  });
  rlEnd();
  rlDrawRenderBatchActive();
  rlEnableDepthTest();
}

}  // namespace z13::raylib
