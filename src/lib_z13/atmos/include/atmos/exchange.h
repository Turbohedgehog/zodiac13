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

#include <cstddef>
#include <limits>
#include <span>

#include <atmos/air.h>

namespace z13::utility::atmos {

// The vacuum outside every room: it has no air and takes whatever comes.
inline constexpr size_t kOutside = std::numeric_limits<size_t>::max();

struct Chamber {
  Air air;
  int64_t volume_cells {};
};

// An opening of `area_cells` cells between two chambers, either of which may be kOutside.
struct Link {
  size_t a {};
  size_t b {};
  int64_t area_cells {};
};

struct ExchangeSettings {
  // The share of the chamber's air, in millionths, that goes through an opening, per thousandth of the ratio of its
  // area to the chamber's volume, at a pressure difference of the whole pressure.
  int64_t coefficient {};
  // The most of a chamber's air, in millionths, that all its openings take at one step.
  int64_t max_outflow_ppm {};
};

inline constexpr int64_t kPartsPerMillion = 1'000'000;

// One step of air through the links, worked out from the air the chambers have at its start: from the higher pressure
// to the lower, by the opening's area and the difference, never so far that the pressures cross, and never more than a
// chamber has. What goes outside is returned; apart from it nothing is made or lost, to the last millimole and
// millijoule. The links are taken in the order given.
Air Exchange(std::span<Chamber> chambers, std::span<const Link> links, const ExchangeSettings& settings);

}  // namespace z13::utility::atmos
