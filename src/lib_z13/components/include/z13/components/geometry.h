#pragma once

namespace z13::geometry {

struct Vector3 {
  float x;
  float y;
  float z;

  static constinit Vector3 kZero;
  static constinit Vector3 kOne;
};

inline constinit Vector3 Vector3::kZero = {0.f, 0.f, 0.f};
inline constinit Vector3 Vector3::kOne = {1.f, 1.f, 1.f};

struct Quaternion {
  float x;
  float y;
  float z;
  float w;

  static constinit Quaternion kIdentity; 
};

inline constinit Quaternion Quaternion::kIdentity = {0.f, 0.f, 0.f, 1.f};

struct Transform {
  Vector3 position;
  Quaternion rotation;
  Vector3 scale;

  static const Transform kIdentity;
};

inline const Transform Transform::kIdentity = {
  .position = Vector3::kZero,
  .rotation = Quaternion::kIdentity,
  .scale = Vector3::kOne,
};

}  // namespace z13::geometry
