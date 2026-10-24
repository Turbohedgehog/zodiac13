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

#include <cstdint>
#include <optional>
#include <string_view>

#include <z13_settings/net_tuning.h>

namespace z13::net {

struct HintClassTuning {
  uint64_t interval_ticks {};
  fbs::net::HintFilter filter {};
  double radius_m {};
};

// The class an action's `hint` attribute names (common.fbs); a new class is its fields in
// net_tuning.fbs and a row in hint_classes.cpp. Nullopt for a name no class has.
std::optional<HintClassTuning> FindHintClass(const z13::NetTuning& tuning, std::string_view name);

}  // namespace z13::net
