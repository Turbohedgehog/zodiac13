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

#include <array>
#include <cmath>
#include <cstdint>

namespace z13::gameplay {

// Linear RGB, each channel in [0, 1].
using Rgb = std::array<float, 3>;

inline constexpr double kPlayerHueStep = 0.6180339887498949;  // golden ratio conjugate
inline constexpr float kPlayerColorSaturation = 0.75f;
inline constexpr float kPlayerColorValue = 0.95f;

// Pure and deterministic, so it never goes over the wire. Golden-ratio hue steps keep
// any run of consecutive ids far apart on the colour wheel.
inline Rgb PlayerColor(uint32_t player_id) {
  constexpr int kHueSectors = 6;
  const double hue = std::fmod(static_cast<double>(player_id) * kPlayerHueStep, 1.0);
  const double scaled = hue * kHueSectors;
  const int sector = static_cast<int>(scaled) % kHueSectors;
  const float fraction = static_cast<float>(scaled - std::floor(scaled));

  const float v = kPlayerColorValue;
  const float p = v * (1.f - kPlayerColorSaturation);
  const float q = v * (1.f - kPlayerColorSaturation * fraction);
  const float t = v * (1.f - kPlayerColorSaturation * (1.f - fraction));
  switch (sector) {
    case 0:
      return {v, t, p};
    case 1:
      return {q, v, p};
    case 2:
      return {p, v, t};
    case 3:
      return {p, q, v};
    case 4:
      return {t, p, v};
    default:
      return {v, p, q};
  }
}

}  // namespace z13::gameplay
