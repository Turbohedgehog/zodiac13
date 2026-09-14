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

#include "platform/sdl_platform.h"

#include <array>
#include <cstdarg>
#include <cstdio>

#include <SDL3/SDL.h>

#include <raylib.h>  // SetTraceLogCallback only (no InitWindow / CORE)
#include <rlgl.h>
#include <spdlog/spdlog.h>

namespace z13::raylib {

namespace {

// The one piece of state that must stay global: render_components.h's GPU-resource
// RAII wrappers live in shared_ptrs inside flecs components and can be destructed
// from anywhere, with no SdlPlatform instance reachable to ask. Plain bool: the
// engine is single-threaded today (Core::Run() is one loop), so there's nothing to
// race against yet -- revisit if that changes.
bool gl_context_alive = false;

void ForwardRaylibLog(int level, const char* text, va_list args) {
  std::array<char, 512> buffer;
  std::vsnprintf(buffer.data(), buffer.size(), text, args);
  switch (level) {
    case LOG_FATAL:
      [[fallthrough]];
    case LOG_ERROR:
      spdlog::error("[raylib] {}", buffer.data());
      break;
    case LOG_WARNING:
      spdlog::warn("[raylib] {}", buffer.data());
      break;
    case LOG_INFO:
      spdlog::info("[raylib] {}", buffer.data());
      break;
    default:
      spdlog::debug("[raylib] {}", buffer.data());
      break;
  }
}

}  // namespace

bool IsGlContextAlive() { return gl_context_alive; }

SdlPlatform::SdlPlatform() = default;
SdlPlatform::~SdlPlatform() { Shutdown(); }

bool SdlPlatform::Init(int width, int height, const char* title) {
  SetTraceLogCallback(ForwardRaylibLog);
  SetTraceLogLevel(LOG_WARNING);

  // Core owns the process lifecycle; don't let SDL turn a console Ctrl+C / SIGTERM
  // into a spurious SDL_EVENT_QUIT.
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    spdlog::error("[sdl] SDL_Init: {}", SDL_GetError());
    return false;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  SDL_Window* window = SDL_CreateWindow(title, width, height,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (window == nullptr) {
    spdlog::error("[sdl] SDL_CreateWindow: {}", SDL_GetError());
    return false;
  }
  window_ = window;

  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (gl == nullptr) {
    spdlog::error("[sdl] SDL_GL_CreateContext: {}", SDL_GetError());
    return false;
  }
  gl_ = gl;
  SDL_GL_MakeCurrent(window, gl);
  SDL_GL_SetSwapInterval(0);  // Core owns frame pacing

  rlLoadExtensions(reinterpret_cast<void*>(SDL_GL_GetProcAddress));
  rlglInit(width, height);

  size_ = {width, height};
  quit_ = false;
  events_.reserve(64);
  gl_context_alive = true;

  spdlog::info("[sdl] window {}x{} '{}' + GL context created", width, height, title);
  return true;
}

void SdlPlatform::Shutdown() {
  // Flip before tearing anything down: any GPU-resource destructor that runs
  // during/after this must see the context as gone.
  gl_context_alive = false;

  if (gl_ != nullptr) {
    rlglClose();
    SDL_GL_DestroyContext(gl_);
    gl_ = nullptr;
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
}

bool SdlPlatform::IsReady() const { return window_ != nullptr && gl_ != nullptr; }

SDL_Window* SdlPlatform::Window() const { return window_; }
SDL_GLContextState* SdlPlatform::GlContext() const { return gl_; }

void SdlPlatform::PumpEvents() {
  events_.clear();
  mouse_delta_ = Eigen::Vector2f::Zero();

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    events_.push_back(event);

    switch (event.type) {
      case SDL_EVENT_QUIT:
        [[fallthrough]];
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        spdlog::info("[sdl] close requested (event 0x{:x})", event.type);
        quit_ = true;
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        [[fallthrough]];
      case SDL_EVENT_WINDOW_RESIZED:
        size_ = {event.window.data1, event.window.data2};
        break;
      case SDL_EVENT_MOUSE_MOTION:
        mouse_delta_ += Eigen::Vector2f{event.motion.xrel, event.motion.yrel};
        mouse_pos_ = {event.motion.x, event.motion.y};
        break;
      default:
        break;
    }
  }
}

bool SdlPlatform::QuitRequested() const { return quit_; }

void SdlPlatform::BeginFrame(unsigned char r, unsigned char g, unsigned char b) {
  rlViewport(0, 0, size_.x(), size_.y());
  rlClearColor(r, g, b, 255);
  rlClearScreenBuffers();
}

void SdlPlatform::EndFrame() {
  rlDrawRenderBatchActive();
  // Plan section 4: return to a plain GL state before SwapBuffers (helps against
  // overlay injectors that are sensitive to unusual state at present).
  rlDisableFramebuffer();
  rlDisableVertexArray();
  rlDisableShader();
  rlDisableScissorTest();
  rlDisableWireMode();
  rlSetBlendMode(RL_BLEND_ALPHA);
  rlViewport(0, 0, size_.x(), size_.y());
  SDL_GL_SwapWindow(window_);
}

Eigen::Vector2i SdlPlatform::Size() const { return size_; }

void SdlPlatform::SetRelativeMouse(bool enabled) {
  if (enabled == relative_mouse_) {
    return;
  }
  if (!enabled) {
    // Recentre before releasing the grab so the OS cursor doesn't reappear at a
    // stale position from before relative mode was entered.
    SDL_WarpMouseInWindow(window_, static_cast<float>(size_.x()) / 2.f,
                          static_cast<float>(size_.y()) / 2.f);
  }
  // Read back the actual result rather than trusting the request: SDL can silently
  // fail to grab relative mode right after window creation (focus not settled yet),
  // and caching the requested value would then wedge the toggle forever.
  SDL_SetWindowRelativeMouseMode(window_, enabled);
  relative_mouse_ = SDL_GetWindowRelativeMouseMode(window_);
  spdlog::info("[sdl] SetRelativeMouse(requested={}) -> actual={}", enabled, relative_mouse_);
}

bool SdlPlatform::RelativeMouseEnabled() const { return relative_mouse_; }

Eigen::Vector2f SdlPlatform::MouseDelta() const { return mouse_delta_; }

Eigen::Vector2f SdlPlatform::MousePosition() const { return mouse_pos_; }

const std::vector<SDL_Event>& SdlPlatform::FrameEvents() const { return events_; }

}  // namespace z13::raylib
