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

#include "scene_camera.h"

#include <raymath.h>
#include <rlgl.h>

#include <lib_core/utils/log.h>
#include <z13/components/input/action_listener.h>

#include "../tools/math_convert.h"

namespace z13::raylib {

void EnsureRaylibCamera(flecs::entity e, const RaylibData&, const gameplay::Camera& camera) {
  ::Camera3D cam{};
  cam.fovy = camera.fov;
  cam.projection = CAMERA_PERSPECTIVE;
  cam.position = {0.f, 0.f, 0.f};
  cam.target = {1.f, 0.f, 0.f};
  cam.up = {0.f, 0.f, 1.f};

  if (const auto* transform = e.try_get<Eigen::Matrix4f>()) {
    UpdateCameraFromTransform(cam, *transform);
  }

  e.set<RaylibCamera>({.camera = cam});
  log_info("RaylibCamera created for '{}' (fov {})", e.name().c_str(), camera.fov);
}

void SyncRaylibCamera(
    RaylibCamera& raylib_camera, const gameplay::Camera& camera, const Eigen::Matrix4f& transform) {
  raylib_camera.camera.fovy = camera.fov;
  UpdateCameraFromTransform(raylib_camera.camera, transform);
}

void ReleaseOrphanRaylibCamera(flecs::entity e, const RaylibCamera&) {
  if (!e.has<gameplay::Camera>() || !e.has<z13::input::CurrentActionListenerTag>()) {
    e.remove<RaylibCamera>();
  }
}

Eigen::Vector2f PixelsInNdc(float pixels, const Eigen::Vector2i& screen) {
  constexpr float kNdcSpan = 2.f;
  return (kNdcSpan * pixels) * screen.cast<float>().cwiseMax(1.f).cwiseInverse();
}

Eigen::Matrix4f CurrentViewProjection() {
  return RaylibToEigenMatrix(rlGetMatrixProjection()) * RaylibToEigenMatrix(rlGetMatrixModelview());
}

void BeginScene3D(const ::Camera3D& camera, const Eigen::Vector2i& size) {
  rlDrawRenderBatchActive();
  const float aspect =
      size.y() > 0 ? static_cast<float>(size.x()) / static_cast<float>(size.y()) : 1.f;
  const ::Matrix projection = MatrixPerspective(camera.fovy * DEG2RAD, aspect,
                                                rlGetCullDistanceNear(), rlGetCullDistanceFar());
  rlSetMatrixProjection(projection);
  rlSetMatrixModelview(MatrixLookAt(camera.position, camera.target, camera.up));
  rlEnableDepthTest();
}

void EndScene3D() {
  rlDrawRenderBatchActive();
  rlDisableDepthTest();
}

}  // namespace z13::raylib
