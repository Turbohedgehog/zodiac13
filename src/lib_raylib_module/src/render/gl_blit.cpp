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

#include "gl_blit.h"

#include "gl_volume_functions.h"

namespace z13::raylib {

struct GlBlitFunctions {
  void(APIENTRY* bind_framebuffer)(GLenum, GLuint) {};
  PFNGLBLITFRAMEBUFFERPROC blit_framebuffer {};
};

GlBlit::GlBlit() {
  auto functions = std::make_shared<GlBlitFunctions>();
  functions->bind_framebuffer = GlLoad<decltype(functions->bind_framebuffer)>("glBindFramebuffer");
  functions->blit_framebuffer = GlLoad<PFNGLBLITFRAMEBUFFERPROC>("glBlitFramebuffer");
  if (functions->bind_framebuffer && functions->blit_framebuffer) {
    functions_ = std::move(functions);
  }
}

void GlBlit::ToWindow(unsigned int framebuffer, const Eigen::Vector2i& from, const Eigen::Vector2i& to) const {
  if (!functions_) {
    return;
  }
  const GlBlitFunctions& gl = *functions_;
  constexpr GLuint kWindowFramebuffer = 0;
  gl.bind_framebuffer(GL_READ_FRAMEBUFFER, framebuffer);
  gl.bind_framebuffer(GL_DRAW_FRAMEBUFFER, kWindowFramebuffer);
  gl.blit_framebuffer(0, 0, from.x(), from.y(), 0, 0, to.x(), to.y(), GL_COLOR_BUFFER_BIT, GL_LINEAR);
  gl.bind_framebuffer(GL_FRAMEBUFFER, kWindowFramebuffer);
}

}  // namespace z13::raylib
