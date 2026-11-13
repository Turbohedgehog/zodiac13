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
#include <optional>

#include <Eigen/Dense>

#include <lib_core/utils/status.h>

#include "gl_blit.h"

namespace z13::raylib {

// What the frame is cleared to, the window's and the scene target's alike.
inline constexpr std::array<unsigned char, 3> kClearColor = {28, 28, 38};

// The offscreen framebuffer the 3D scene is drawn into when it is rendered smaller than the window
// (RenderTuning::resolution_scale), and then stretched over it. Never state.
struct SceneTarget {
  using Singleton = void;

  unsigned int framebuffer {};
  Eigen::Vector2i size = Eigen::Vector2i::Zero();
  // The size that failed, so a driver that refuses it isn't asked every frame.
  std::optional<Eigen::Vector2i> failed;
  std::optional<GlBlit> blit;

  bool Ready() const { return framebuffer != 0; }
};

// `window` scaled by `scale` (clamped to 0.25..1 by the settings), at least one pixel each way.
Eigen::Vector2i ScaledSize(const Eigen::Vector2i& window, float scale);

// Makes the target `size`; an error only the first time a size fails, after which Ready() is false.
z13::Status FitSceneTarget(SceneTarget& target, const Eigen::Vector2i& size);
void ClearSceneTarget(const SceneTarget& target);
void EnterSceneTarget(const SceneTarget& target);
void PresentSceneTarget(const SceneTarget& target, const Eigen::Vector2i& window);

}  // namespace z13::raylib
