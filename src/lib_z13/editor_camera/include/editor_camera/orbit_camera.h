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

#include <optional>

#include <Eigen/Dense>

#include <z13/components/editor/editor_camera.h>

namespace z13::editor::camera {

struct DistanceLimits {
  float min {};
  float max {};
};

// A ray from the eye, in cells; `direction` has length 1.
struct Ray {
  Eigen::Vector3f origin = Eigen::Vector3f::Zero();
  Eigen::Vector3f direction = Eigen::Vector3f::Zero();
};

// What two fingers did between two moments, as one gesture.
struct Gesture {
  Eigen::Vector2f pan_pixels = Eigen::Vector2f::Zero();
  // Of the distance: below 1 when the fingers moved apart.
  float zoom_factor = 1.f;
  // Clockwise on the screen.
  float twist_degrees {};
};

Eigen::Vector3f EyeOf(const OrbitCamera& camera);

// A drag turns the scene with the pointer: right turns it right, down tips it to look from above.
OrbitCamera Orbited(const OrbitCamera& camera, const Eigen::Vector2f& drag_pixels, float degrees_per_pixel);

// A drag grabs the ground: it moves under the pointer, along the floor whatever the pitch.
OrbitCamera Panned(const OrbitCamera& camera, const Eigen::Vector2f& drag_pixels, float per_pixel);

// A factor that is not above 0 leaves the camera where it is.
OrbitCamera Zoomed(const OrbitCamera& camera, float factor, const DistanceLimits& limits);

// The factor of one wheel notch: `per_notch` farther, its inverse nearer; 1, no zoom, when `per_notch` is not above 0.
float NotchFactor(float per_notch, bool nearer);

OrbitCamera Turned(const OrbitCamera& camera, float yaw_degrees);

// Looks at the middle of the box (corner and extent, in cells) from far enough to see all of it; a field of view too
// narrow to divide by gives the farthest distance.
OrbitCamera Framed(const OrbitCamera& camera, const Eigen::Vector3f& min, const Eigen::Vector3f& extent,
                   float fov_degrees, const DistanceLimits& limits);

// `ndc` is the point on the screen, x to the right and y up, each from -1 to 1.
Ray RayThrough(const OrbitCamera& camera, const Eigen::Vector2f& ndc, float fov_degrees, float aspect);

// Where the ray meets the horizontal plane at height `z`; none for a ray that runs level or away from it.
std::optional<Eigen::Vector3f> GroundHit(const Ray& ray, float z);

Gesture TwoFingerGesture(const Eigen::Vector2f& first_before, const Eigen::Vector2f& second_before,
                         const Eigen::Vector2f& first_after, const Eigen::Vector2f& second_after);

}  // namespace z13::editor::camera
