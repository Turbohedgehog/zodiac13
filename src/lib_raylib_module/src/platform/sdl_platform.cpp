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

#include <cstdarg>
#include <cstdio>
#include <vector>

#include <SDL3/SDL.h>

#include <raylib.h>  // SetTraceLogCallback only (no InitWindow / CORE)
#include <rlgl.h>
#include <spdlog/spdlog.h>

namespace z13::raylib {

namespace {

SDL_Window* g_window = nullptr;
SDL_GLContext g_gl = nullptr;
int g_width = 0;
int g_height = 0;
bool g_quit = false;
bool g_relative_mouse = false;
int g_mouse_dx = 0;
int g_mouse_dy = 0;
int g_mouse_x = 0;
int g_mouse_y = 0;
std::vector<SDL_Event> g_events;

void ForwardRaylibLog(int level, const char* text, va_list args) {
  char buffer[512];
  std::vsnprintf(buffer, sizeof(buffer), text, args);
  switch (level) {
    case LOG_FATAL:
    case LOG_ERROR:
      spdlog::error("[raylib] {}", buffer);
      break;
    case LOG_WARNING:
      spdlog::warn("[raylib] {}", buffer);
      break;
    case LOG_INFO:
      spdlog::info("[raylib] {}", buffer);
      break;
    default:
      spdlog::debug("[raylib] {}", buffer);
      break;
  }
}

// Plan section 4: return to a plain GL state before SwapBuffers (helps against
// overlay injectors that are sensitive to unusual state at present).
void ResetGLStateToBaseline() {
  rlDisableFramebuffer();
  rlDisableVertexArray();
  rlDisableShader();
  rlDisableScissorTest();
  rlDisableWireMode();
  rlSetBlendMode(RL_BLEND_ALPHA);
  rlViewport(0, 0, g_width, g_height);
}

}  // namespace

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

  g_window = SDL_CreateWindow(title, width, height,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (g_window == nullptr) {
    spdlog::error("[sdl] SDL_CreateWindow: {}", SDL_GetError());
    return false;
  }

  g_gl = SDL_GL_CreateContext(g_window);
  if (g_gl == nullptr) {
    spdlog::error("[sdl] SDL_GL_CreateContext: {}", SDL_GetError());
    return false;
  }
  SDL_GL_MakeCurrent(g_window, g_gl);
  SDL_GL_SetSwapInterval(0);  // Core owns frame pacing

  rlLoadExtensions(reinterpret_cast<void*>(SDL_GL_GetProcAddress));
  rlglInit(width, height);

  g_width = width;
  g_height = height;
  g_quit = false;
  g_events.reserve(64);

  spdlog::info("[sdl] window {}x{} '{}' + GL context created", width, height, title);
  return true;
}

void SdlPlatform::Shutdown() {
  if (g_gl != nullptr) {
    rlglClose();
    SDL_GL_DestroyContext(g_gl);
    g_gl = nullptr;
  }
  if (g_window != nullptr) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
  SDL_Quit();
}

bool SdlPlatform::IsReady() { return g_window != nullptr && g_gl != nullptr; }

void* SdlPlatform::Window() { return g_window; }
void* SdlPlatform::GlContext() { return g_gl; }

void SdlPlatform::PumpEvents() {
  g_events.clear();
  g_mouse_dx = 0;
  g_mouse_dy = 0;

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    g_events.push_back(event);

    switch (event.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        spdlog::info("[sdl] close requested (event 0x{:x})", event.type);
        g_quit = true;
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      case SDL_EVENT_WINDOW_RESIZED:
        g_width = event.window.data1;
        g_height = event.window.data2;
        break;
      case SDL_EVENT_MOUSE_MOTION:
        g_mouse_dx += static_cast<int>(event.motion.xrel);
        g_mouse_dy += static_cast<int>(event.motion.yrel);
        g_mouse_x = static_cast<int>(event.motion.x);
        g_mouse_y = static_cast<int>(event.motion.y);
        break;
      default:
        break;
    }
  }
}

bool SdlPlatform::QuitRequested() { return g_quit; }

void SdlPlatform::BeginFrame(unsigned char r, unsigned char g, unsigned char b) {
  rlViewport(0, 0, g_width, g_height);
  rlClearColor(r, g, b, 255);
  rlClearScreenBuffers();
}

void SdlPlatform::EndFrame() {
  rlDrawRenderBatchActive();
  ResetGLStateToBaseline();
  SDL_GL_SwapWindow(g_window);
}

int SdlPlatform::Width() { return g_width; }
int SdlPlatform::Height() { return g_height; }

void SdlPlatform::SetRelativeMouse(bool enabled) {
  if (enabled == g_relative_mouse) {
    return;
  }
  g_relative_mouse = enabled;
  SDL_SetWindowRelativeMouseMode(g_window, enabled);
}

bool SdlPlatform::RelativeMouseEnabled() { return g_relative_mouse; }

void SdlPlatform::MouseDelta(int* dx, int* dy) {
  if (dx != nullptr) *dx = g_mouse_dx;
  if (dy != nullptr) *dy = g_mouse_dy;
}

void SdlPlatform::MousePosition(int* x, int* y) {
  if (x != nullptr) *x = g_mouse_x;
  if (y != nullptr) *y = g_mouse_y;
}

const SDL_Event* SdlPlatform::FrameEvents(int* count) {
  if (count != nullptr) {
    *count = static_cast<int>(g_events.size());
  }
  return g_events.empty() ? nullptr : g_events.data();
}

}  // namespace z13::raylib
