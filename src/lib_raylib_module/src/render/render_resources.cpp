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

#include "render_resources.h"

#include <rlgl.h>

#include "../platform/sdl_platform.h"

namespace z13::raylib {

void SafeUnloadShader(::Shader& shader) {
  if (shader.id != 0 && shader.id != rlGetShaderIdDefault() && IsGlContextAlive()) {
    UnloadShader(shader);
  }
  shader = {};
}

void SafeUnloadTexture(::Texture2D& texture) {
  if (texture.id != 0 && texture.id != rlGetTextureIdDefault() && IsGlContextAlive()) {
    UnloadTexture(texture);
  }
  texture = {};
}

std::shared_ptr<::Shader> MakeManagedShader(::Shader shader) {
  return std::shared_ptr<::Shader>(new ::Shader(shader), [](::Shader* s) {
    SafeUnloadShader(*s);
    delete s;
  });
}

std::shared_ptr<::Texture2D> MakeManagedTexture(::Texture2D texture) {
  return std::shared_ptr<::Texture2D>(new ::Texture2D(texture), [](::Texture2D* t) {
    SafeUnloadTexture(*t);
    delete t;
  });
}

std::shared_ptr<::Model> MakeManagedModel(::Model model, unsigned int borrowed_shader_id) {
  return std::shared_ptr<::Model>(new ::Model(model), [borrowed_shader_id](::Model* m) {
    if (m->meshCount == 0 || !IsGlContextAlive()) {
      delete m;
      return;
    }
    for (int i = 0; i < m->materialCount; ++i) {
      if (m->materials[i].shader.id == borrowed_shader_id) {
        m->materials[i].shader.id = 0;
      }
      // UnloadModel doesn't free material textures; free any the model actually loaded.
      for (int map = 0; map <= MATERIAL_MAP_BRDF; ++map) {
        if (m->materials[i].maps[map].texture.id != 0) {
          SafeUnloadTexture(m->materials[i].maps[map].texture);
        }
      }
    }
    UnloadModel(*m);
    delete m;
  });
}

}  // namespace z13::raylib
