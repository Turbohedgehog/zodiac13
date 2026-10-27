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

#include "hint_classes.h"

#include <algorithm>
#include <array>

namespace z13::net {

namespace {

struct HintClassFields {
  std::string_view name;
  uint64_t fbs::net::NetTuningT::* interval_ticks {};
  fbs::net::HintFilter fbs::net::NetTuningT::* filter {};
  double fbs::net::NetTuningT::* radius_m {};
  double fbs::net::NetTuningT::* near_radius_m {};
  uint64_t fbs::net::NetTuningT::* far_every {};
};

constexpr std::array kClasses {
    HintClassFields {.name = "look",
                     .interval_ticks = &fbs::net::NetTuningT::look_hint_interval_ticks,
                     .filter = &fbs::net::NetTuningT::look_hint_filter,
                     .radius_m = &fbs::net::NetTuningT::look_hint_radius_m,
                     .near_radius_m = &fbs::net::NetTuningT::look_hint_near_radius_m,
                     .far_every = &fbs::net::NetTuningT::look_hint_far_every},
};

}  // namespace

std::optional<HintClassTuning> FindHintClass(const z13::NetTuning& tuning, std::string_view name) {
  const auto found = std::ranges::find(kClasses, name, &HintClassFields::name);
  if (found == kClasses.end()) {
    return std::nullopt;
  }
  return HintClassTuning {
      .interval_ticks = tuning.*(found->interval_ticks),
      .filter = tuning.*(found->filter),
      .radius_m = tuning.*(found->radius_m),
      .near_radius_m = tuning.*(found->near_radius_m),
      .far_every = tuning.*(found->far_every),
  };
}

}  // namespace z13::net
