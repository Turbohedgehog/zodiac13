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

#include "render_diagnostics.h"

#include <format>
#include <string>

#include <imgui.h>

#include <z13_settings/settings.h>

#include "platform/sdl_platform.h"
#include "render/cell_lighting.h"
#include "render/shadow_maps.h"

namespace z13::raylib::gui {

namespace {

void Line(const std::string& text) {
  ImGui::TextUnformatted(text.c_str());
}

}  // namespace

void DrawRenderDiagnostics(flecs::world world) {
  if (!ImGui::CollapsingHeader("Diagnostics")) {
    return;
  }
  if (const auto* platform = world.try_get<SdlPlatformData>(); platform != nullptr && platform->platform) {
    const GlInfo& gl = platform->platform->Gl();
    Line(std::format("GPU: {}", gl.renderer));
    Line(std::format("GL: {}", gl.version));
    Line(std::format("Float color buffers: {}", gl.float_color_buffer ? "yes" : "no"));
  }
  if (const auto* shadows = world.try_get<ShadowMaps>(); shadows != nullptr) {
    const ShadowHealth health = shadows->Health();
    Line(std::format("Lamp shadow slots: {}{}", health.lamp_slots, health.atlas_failed ? " (atlas failed)" : ""));
    Line(health.last_gl_error ? std::format("GL errors: {}, last 0x{:04X}", health.gl_errors, *health.last_gl_error)
                              : std::string("GL errors: none"));
  }
  if (const auto* cell_lighting = world.try_get<CellLighting>(); cell_lighting != nullptr) {
    const auto failure = cell_lighting->Failure();
    Line(failure ? std::format("Baked lamp light: off, {}", *failure) : std::string("Baked lamp light: on"));
  }
  if (auto* tuning = world.try_get_mut<z13::RenderTuning>(); tuning != nullptr) {
    bool lamp_shadows = tuning->shadow_lights > 0;
    if (ImGui::Checkbox("Lamp shadows", &lamp_shadows)) {
      tuning->shadow_lights = lamp_shadows ? z13::RenderTuning {}.shadow_lights : 0;
    }
  }
}

}  // namespace z13::raylib::gui
