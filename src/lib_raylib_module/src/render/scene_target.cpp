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

#include "scene_target.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <rlgl.h>

namespace z13::raylib {

namespace {

constexpr unsigned char kOpaque = 255;

void Release(SceneTarget& target) {
  if (target.framebuffer != 0) {
    rlUnloadFramebuffer(target.framebuffer);
  }
  target.framebuffer = 0;
  target.size = Eigen::Vector2i::Zero();
}

}  // namespace

Eigen::Vector2i ScaledSize(const Eigen::Vector2i& window, float scale) {
  return (window.cast<float>() * scale).array().round().cast<int>().cwiseMax(1).matrix();
}

z13::Status FitSceneTarget(SceneTarget& target, const Eigen::Vector2i& size) {
  if (target.Ready() && target.size == size) {
    return {};
  }
  if (target.failed == size) {
    return {};
  }
  Release(target);
  if (!target.blit) {
    target.blit.emplace();
  }
  const unsigned int framebuffer = rlLoadFramebuffer();
  const unsigned int color = rlLoadTexture(nullptr, size.x(), size.y(), RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
  const unsigned int depth = rlLoadTextureDepth(size.x(), size.y(), true);
  rlFramebufferAttach(framebuffer, color, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  rlFramebufferAttach(framebuffer, depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
  if (framebuffer == 0 || color == 0 || depth == 0 || !rlFramebufferComplete(framebuffer)) {
    if (framebuffer != 0) {
      rlUnloadFramebuffer(framebuffer);
    }
    target.failed = size;
    return std::unexpected(std::format("can't make a scene target of {} x {} px", size.x(), size.y()));
  }
  target.failed.reset();
  target.framebuffer = framebuffer;
  target.size = size;
  return {};
}

void EnterSceneTarget(const SceneTarget& target) {
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(target.framebuffer);
  rlViewport(0, 0, target.size.x(), target.size.y());
}

void ClearSceneTarget(const SceneTarget& target) {
  EnterSceneTarget(target);
  rlClearColor(kClearColor[0], kClearColor[1], kClearColor[2], kOpaque);
  rlClearScreenBuffers();
}

void PresentSceneTarget(const SceneTarget& target, const Eigen::Vector2i& window) {
  rlDrawRenderBatchActive();
  target.blit->ToWindow(target.framebuffer, target.size, window);
  rlDisableFramebuffer();
  rlViewport(0, 0, window.x(), window.y());
}

}  // namespace z13::raylib
