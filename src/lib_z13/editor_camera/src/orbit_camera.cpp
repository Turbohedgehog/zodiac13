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

#include "editor_camera/orbit_camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace z13::editor::camera {

namespace {

// Just short of straight down or up, where the screen's up and the camera's forward would line up.
constexpr float kPitchLimitDegrees = 89.f;
// Fingers closer than this give no steady ratio: the gesture is not a pinch.
constexpr float kMinFingerGapPixels = 1.f;
// Under this the tangent of half the field of view is no usable divisor: the view is taken as unframeable.
constexpr float kMinHalfFovTangent = 1e-3f;
// A ray whose height changes by less than this per unit of its length is level: no usable divisor.
constexpr float kLevelSlope = 1e-4f;
// A fifth of the view around what is framed.
constexpr float kFrameMargin = 1.2f;
constexpr float kHalf = 0.5f;
constexpr float kHalfTurnDegrees = 180.f;
constexpr float kFullTurnDegrees = 360.f;

const Eigen::Vector3f kUp = Eigen::Vector3f::UnitZ();

float ToRadians(float degrees) {
  return degrees * std::numbers::pi_v<float> / kHalfTurnDegrees;
}

float ToDegrees(float radians) {
  return radians * kHalfTurnDegrees / std::numbers::pi_v<float>;
}

Eigen::Vector3f OffsetOf(const OrbitCamera& camera) {
  const float yaw = ToRadians(camera.yaw_degrees);
  const float pitch = ToRadians(camera.pitch_degrees);
  return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
}

// From the eye towards the target along the floor.
Eigen::Vector3f GroundForward(const OrbitCamera& camera) {
  const Eigen::Vector3f offset = OffsetOf(camera);
  return Eigen::Vector3f(-offset.x(), -offset.y(), 0.f).normalized();
}

Eigen::Vector3f RightOf(const Eigen::Vector3f& forward) {
  return forward.cross(kUp).normalized();
}

float Wrapped(float degrees) {
  return degrees - kFullTurnDegrees * std::round(degrees / kFullTurnDegrees);
}

}  // namespace

Eigen::Vector3f EyeOf(const OrbitCamera& camera) {
  return camera.target + camera.distance * OffsetOf(camera);
}

OrbitCamera Orbited(const OrbitCamera& camera, const Eigen::Vector2f& drag_pixels, float degrees_per_pixel) {
  OrbitCamera turned = camera;
  turned.yaw_degrees = Wrapped(camera.yaw_degrees - drag_pixels.x() * degrees_per_pixel);
  turned.pitch_degrees = std::clamp(camera.pitch_degrees + drag_pixels.y() * degrees_per_pixel, -kPitchLimitDegrees,
                                    kPitchLimitDegrees);
  return turned;
}

OrbitCamera Panned(const OrbitCamera& camera, const Eigen::Vector2f& drag_pixels, float per_pixel) {
  const Eigen::Vector3f forward = GroundForward(camera);
  const float scale = per_pixel * camera.distance;
  OrbitCamera moved = camera;
  moved.target += forward * (drag_pixels.y() * scale) - RightOf(forward) * (drag_pixels.x() * scale);
  return moved;
}

float NotchFactor(float per_notch, bool nearer) {
  if (per_notch <= 0.f) {
    return 1.f;
  }
  return nearer ? 1.f / per_notch : per_notch;
}

OrbitCamera Zoomed(const OrbitCamera& camera, float factor, const DistanceLimits& limits) {
  if (std::isnan(factor) || factor <= 0.f) {
    return camera;
  }
  OrbitCamera zoomed = camera;
  zoomed.distance = std::clamp(camera.distance * factor, limits.min, limits.max);
  return zoomed;
}

OrbitCamera Turned(const OrbitCamera& camera, float yaw_degrees) {
  OrbitCamera turned = camera;
  turned.yaw_degrees = Wrapped(camera.yaw_degrees + yaw_degrees);
  return turned;
}

OrbitCamera Framed(const OrbitCamera& camera, const Eigen::Vector3f& min, const Eigen::Vector3f& extent,
                   float fov_degrees, const DistanceLimits& limits) {
  OrbitCamera framed = camera;
  framed.target = min + extent * kHalf;
  const float radius = extent.norm() * kHalf;
  const float half_fov_tangent = std::tan(ToRadians(fov_degrees) * kHalf);
  framed.distance = half_fov_tangent >= kMinHalfFovTangent
                        ? std::clamp(kFrameMargin * radius / half_fov_tangent, limits.min, limits.max)
                        : limits.max;
  return framed;
}

Ray RayThrough(const OrbitCamera& camera, const Eigen::Vector2f& ndc, float fov_degrees, float aspect) {
  const Eigen::Vector3f eye = EyeOf(camera);
  const Eigen::Vector3f forward = (camera.target - eye).normalized();
  const Eigen::Vector3f right = RightOf(forward);
  const Eigen::Vector3f up = right.cross(forward);
  const float half_height = std::tan(ToRadians(fov_degrees) * kHalf);
  const Eigen::Vector3f through = forward + right * (ndc.x() * half_height * aspect) + up * (ndc.y() * half_height);
  return {.origin = eye, .direction = through.normalized()};
}

std::optional<Eigen::Vector3f> GroundHit(const Ray& ray, float z) {
  const float rise = ray.direction.z();
  if (std::abs(rise) <= kLevelSlope) {
    return std::nullopt;
  }
  const float along = (z - ray.origin.z()) / rise;
  if (along < 0.f) {
    return std::nullopt;
  }
  return ray.origin + ray.direction * along;
}

Gesture TwoFingerGesture(const Eigen::Vector2f& first_before, const Eigen::Vector2f& second_before,
                         const Eigen::Vector2f& first_after, const Eigen::Vector2f& second_after) {
  const Eigen::Vector2f apart_before = second_before - first_before;
  const Eigen::Vector2f apart_after = second_after - first_after;
  Gesture gesture;
  gesture.pan_pixels = (first_after + second_after) * kHalf - (first_before + second_before) * kHalf;
  if (apart_before.norm() < kMinFingerGapPixels || apart_after.norm() < kMinFingerGapPixels) {
    return gesture;
  }
  gesture.zoom_factor = apart_before.norm() / apart_after.norm();
  // The screen's y runs down, so a growing angle is a clockwise turn.
  const float turned = std::atan2(apart_after.y(), apart_after.x()) - std::atan2(apart_before.y(), apart_before.x());
  gesture.twist_degrees = Wrapped(ToDegrees(turned));
  return gesture;
}

}  // namespace z13::editor::camera
