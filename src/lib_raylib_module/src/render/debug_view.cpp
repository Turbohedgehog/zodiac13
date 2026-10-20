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

#include "debug_view.h"

#include <array>
#include <string_view>

#include <lib_core/utils/log.h>

#include "render_components.h"

namespace z13::raylib {

namespace {

constexpr std::string_view kDebugViewUniform = "debugView";
constexpr std::array<std::string_view, kDebugViewCount> kDebugViewNames = {
    "normal",
    "white albedo, full light",
    "baked light only",
    "baked light's node cubes",
    "baked light less the players' shadows",
    "cluster lamps and flashlights",
    "sun",
    "fill",
};

}  // namespace

void StepDebugView(const Lighting& lighting, int step) {
  if (!lighting.res) {
    return;
  }
  LightingResources& res = *lighting.res;
  res.debug_view = (res.debug_view + step + kDebugViewCount) % kDebugViewCount;
  SetShaderValue(*res.shader, GetShaderLocation(*res.shader, kDebugViewUniform.data()), &res.debug_view,
                 SHADER_UNIFORM_INT);
  log_info("[raylib] debug view {}: {}", res.debug_view, DebugViewName(res.debug_view));
}

std::string_view DebugViewName(int view) {
  return view >= 0 && view < kDebugViewCount ? kDebugViewNames[view] : std::string_view {};
}

}  // namespace z13::raylib
