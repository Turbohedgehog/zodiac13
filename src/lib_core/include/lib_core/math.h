#pragma once

#include <Eigen/Dense>

namespace z13::math {

constexpr double kEpsilon = 0.0001f;
constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = kPi / 2.0;
constexpr double kTwoPi = 2.0 * kPi;

template <typename T>
using Vector3 = Eigen::Matrix<T, 3, 1>;
template <typename T>
using Matrix4 = Eigen::Matrix<T, 4, 4>;

template <typename T>
constexpr T ToRadians(T deg_angle) {
  return deg_angle * static_cast<T>(kPi) / static_cast<T>(180.0);
}

template <typename T>
constexpr T ToDegrees(T rad_angle) {
  return rad_angle * static_cast<T>(180.0) / static_cast<T>(kPi);
}

template <typename T>
bool IsNear(const Vector3<T>& v1, const Vector3<T>& v2, T epsilon = static_cast<T>(kEpsilon)) {
  return (v1 - v2).squaredNorm() <= epsilon;
}

template <typename T>
Vector3<T> ExtractTranslation(const Matrix4<T>& matrix) {
  return matrix.col(3).head<3>();
}

template <typename T>
Eigen::Quaternion<T> ExtractQuat(const Matrix4<T>& matrix) {
  return Eigen::Quaternion<T>(matrix.block<3,3>(0,0));
}

template <typename T>
void SetTranslation(const Vector3<T>& translation, Matrix4<T>& matrix) {
  matrix.col(3).head<3>() = translation;
}

}  // namespace z13::math
