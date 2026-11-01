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


#include <atmos/exchange.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace z13::utility::atmos {

namespace {

constexpr int64_t kPermille = 1000;

struct Flow {
  size_t from {};
  size_t to {};
  int64_t ppm {};
};

int64_t PressureOf(std::span<const Chamber> chambers, size_t index) {
  return index == kOutside ? 0 : PressurePascals(chambers[index].air, chambers[index].volume_cells);
}

// The share of `from` that goes through the opening, in millionths.
Flow FlowThrough(std::span<const Chamber> chambers, const Link& link, const ExchangeSettings& settings) {
  const int64_t pressure_a = PressureOf(chambers, link.a);
  const int64_t pressure_b = PressureOf(chambers, link.b);
  if (pressure_a == pressure_b) {
    return {};
  }
  const bool a_higher = pressure_a > pressure_b;
  const size_t from = a_higher ? link.a : link.b;
  const size_t to = a_higher ? link.b : link.a;
  const int64_t higher = std::max(pressure_a, pressure_b);
  const int64_t difference = higher - std::min(pressure_a, pressure_b);
  const int64_t from_volume = std::max<int64_t>(chambers[from].volume_cells, 1);
  // The gas that goes is proportional to the area and the difference, so its share of the room's air to their ratio
  // to the pressure and the volume: a room open to the vacuum empties at one rate to the end.
  const int64_t by_opening = settings.coefficient * kPermille * link.area_cells * difference / (higher * from_volume);
  // Moving this share would level the pressures, no more: past it they would cross.
  int64_t levels = kPartsPerMillion;
  if (to != kOutside) {
    const int64_t to_volume = chambers[to].volume_cells;
    levels = kPartsPerMillion * difference * to_volume / (higher * (from_volume + to_volume));
  }
  return {.from = from, .to = to, .ppm = std::clamp(std::min(by_opening, levels), int64_t {}, kPartsPerMillion)};
}

Air Share(const Air& air, int64_t ppm) {
  Air share;
  for (size_t gas = 0; gas < kGasCount; ++gas) {
    share.millimoles[gas] = air.millimoles[gas] * ppm / kPartsPerMillion;
  }
  share.energy = air.energy * ppm / kPartsPerMillion;
  return share;
}

}  // namespace

Air Exchange(std::span<Chamber> chambers, std::span<const Link> links, const ExchangeSettings& settings) {
  std::vector<Flow> flows;
  std::vector<int64_t> outflow(chambers.size());
  for (const Link& link : links) {
    const Flow flow = FlowThrough(chambers, link, settings);
    if (flow.ppm > 0) {
      flows.push_back(flow);
      outflow[flow.from] += flow.ppm;
    }
  }
  const std::vector<Air> start = [&] {
    std::vector<Air> airs;
    airs.reserve(chambers.size());
    std::ranges::transform(chambers, std::back_inserter(airs), [](const Chamber& chamber) { return chamber.air; });
    return airs;
  }();
  std::vector<Air> delta(chambers.size());
  Air lost;
  for (Flow flow : flows) {
    if (outflow[flow.from] > settings.max_outflow_ppm) {
      flow.ppm = flow.ppm * settings.max_outflow_ppm / outflow[flow.from];
    }
    const Air moved = Share(start[flow.from], flow.ppm);
    delta[flow.from] -= moved;
    if (flow.to == kOutside) {
      lost += moved;
    } else {
      delta[flow.to] += moved;
    }
  }
  for (size_t i = 0; i < chambers.size(); ++i) {
    chambers[i].air += delta[i];
  }
  return lost;
}

}  // namespace z13::utility::atmos
