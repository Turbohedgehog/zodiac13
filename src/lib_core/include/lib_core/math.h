#pragma once

namespace z13::math {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = kPi / 2.0;
constexpr double kTwoPi = 2.0 * kPi;

template <typename T>
constexpr T ToRadians(T deg_angle) {
  return deg_angle * static_cast<T>(kPi) / static_cast<T>(180.0);
}

template <typename T>
constexpr T ToDegrees(T rad_angle) {
  return rad_angle * static_cast<T>(180.0) / static_cast<T>(kPi);
}

}  // namespace z13::math
