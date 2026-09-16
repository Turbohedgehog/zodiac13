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

#include <memory>
#include <vector>

#include <Eigen/Dense>

// Platform layer: SDL owns the window, GL context, event loop and timing; raylib
// is used only as an rlgl render module on top of this context.

union SDL_Event;
struct SDL_Window;
struct SDL_GLContextState;

namespace z13::raylib {

// One instance per world; not a static-method/global-state class, so nothing
// here is implicitly shared across worlds.
class SdlPlatform {
 public:
  // Declared, not defaulted: events_ holds SDL_Event by value, which this header
  // only forward-declares, so ctor/dtor must live in the .cpp.
  SdlPlatform();
  ~SdlPlatform();
  SdlPlatform(const SdlPlatform&) = delete;
  SdlPlatform& operator=(const SdlPlatform&) = delete;

  // SDL_Init + window + GL 3.3 core context + rlglInit. false on failure.
  bool Init(int width, int height, const char* title);
  void Shutdown();
  bool IsReady() const;

  // Native handles for layers that need them (ImGui backends).
  SDL_Window* Window() const;
  SDL_GLContextState* GlContext() const;

  // Drains SDL events for this frame into FrameEvents(); tracks quit + size.
  void PumpEvents();
  bool QuitRequested() const;

  // rlViewport + rlClearColor + rlClearScreenBuffers for the current size.
  void BeginFrame(unsigned char r, unsigned char g, unsigned char b);
  // rlDrawRenderBatchActive + baseline GL state + SDL_GL_SwapWindow.
  void EndFrame();

  Eigen::Vector2i Size() const;

  // Relative-mouse (FPS look) toggle + this-frame accumulated delta.
  void SetRelativeMouse(bool enabled);
  bool RelativeMouseEnabled() const;
  Eigen::Vector2f MouseDelta() const;
  Eigen::Vector2f MousePosition() const;

  // SDL events collected by the last PumpEvents(); valid until the next one.
  const std::vector<SDL_Event>& FrameEvents() const;

 private:
  SDL_Window* window_ {};
  SDL_GLContextState* gl_ {};
  Eigen::Vector2i window_size_ = Eigen::Vector2i::Zero();
  bool quit_ {};
  bool relative_mouse_ {};
  Eigen::Vector2f mouse_delta_ = Eigen::Vector2f::Zero();
  Eigen::Vector2f mouse_pos_ = Eigen::Vector2f::Zero();
  std::vector<SDL_Event> events_;
};

// GPU-resource RAII wrappers can be destructed detached from any world/entity,
// with no SdlPlatform instance reachable -- they ask this instead.
bool IsGlContextAlive();

// Singleton component owning the platform instance for a world. shared_ptr matches
// this module's existing GPU-resource-holding component idiom (Skybox, RenderModel).
struct SdlPlatformData {
  std::shared_ptr<SdlPlatform> platform;
};

}  // namespace z13::raylib
