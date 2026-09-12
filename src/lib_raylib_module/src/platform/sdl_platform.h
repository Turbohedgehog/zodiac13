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

// Platform layer: SDL owns the window, GL context, event loop and timing; raylib
// is used only as an rlgl render module on top of this context. See
// plan-migracii-ogre-raylib-sdl-imgui.md.

union SDL_Event;

namespace z13::raylib {

class SdlPlatform {
 public:
  // SDL_Init + window + GL 3.3 core context + rlglInit. false on failure.
  static bool Init(int width, int height, const char* title);
  static void Shutdown();
  static bool IsReady();

  // Native handles for layers that need them (ImGui backends). Opaque here.
  static void* Window();
  static void* GlContext();

  // Drains SDL events for this frame into FrameEvents(); tracks quit + size.
  static void PumpEvents();
  static bool QuitRequested();

  // rlViewport + rlClearColor + rlClearScreenBuffers for the current size.
  static void BeginFrame(unsigned char r, unsigned char g, unsigned char b);
  // rlDrawRenderBatchActive + baseline GL state + SDL_GL_SwapWindow.
  static void EndFrame();

  static int Width();
  static int Height();

  // Relative-mouse (FPS look) toggle + this-frame accumulated delta.
  static void SetRelativeMouse(bool enabled);
  static bool RelativeMouseEnabled();
  static void MouseDelta(int* dx, int* dy);
  static void MousePosition(int* x, int* y);

  // SDL events collected by the last PumpEvents(); valid until the next one.
  static const SDL_Event* FrameEvents(int* count);
};

}  // namespace z13::raylib
