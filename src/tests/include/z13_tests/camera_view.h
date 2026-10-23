#pragma once

#include <cmath>
#include <numbers>

#include <Eigen/Dense>

#include <rooms/view_culling.h>

namespace z13::testing {

inline constexpr float kTestFovY = std::numbers::pi_v<float> / 3.f;
inline constexpr float kTestAspect = 16.f / 9.f;
inline constexpr float kTestNear = 0.05f;
inline constexpr float kTestFar = 1000.f;

// What a camera at `eye` looking at `target` (meters, z up) sees through a perspective lens, as
// the renderer's culling takes it (OpenGL clip space).
inline z13::station::rooms::CameraView CameraViewFrom(const Eigen::Vector3f& eye, const Eigen::Vector3f& target) {
  Eigen::Vector3f forward = (target - eye).normalized();
  Eigen::Vector3f right = forward.cross(Eigen::Vector3f::UnitZ());
  right = right.norm() < 1e-4f ? Eigen::Vector3f(Eigen::Vector3f::UnitX()) : Eigen::Vector3f(right.normalized());
  const Eigen::Vector3f up = right.cross(forward);
  Eigen::Matrix4f view = Eigen::Matrix4f::Identity();
  view.block<1, 3>(0, 0) = right.transpose();
  view.block<1, 3>(1, 0) = up.transpose();
  view.block<1, 3>(2, 0) = -forward.transpose();
  view.block<3, 1>(0, 3) = -view.block<3, 3>(0, 0) * eye;
  const float focal = 1.f / std::tan(kTestFovY / 2.f);
  Eigen::Matrix4f projection = Eigen::Matrix4f::Zero();
  projection(0, 0) = focal / kTestAspect;
  projection(1, 1) = focal;
  projection(2, 2) = (kTestFar + kTestNear) / (kTestNear - kTestFar);
  projection(2, 3) = 2.f * kTestFar * kTestNear / (kTestNear - kTestFar);
  projection(3, 2) = -1.f;
  return {.view_projection = projection * view, .eye = eye};
}

}  // namespace z13::testing
