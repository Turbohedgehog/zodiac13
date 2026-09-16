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

#include "skybox.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <string_view>

#include <Eigen/Dense>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <spdlog/spdlog.h>

#include "../platform/sdl_platform.h"
#include "../tools/asset_path.h"
#include "render_resources.h"

namespace z13::raylib {

namespace {

// CUBEMAP_LAYOUT_LINE_HORIZONTAL face order: +X, -X, +Y, -Y, +Z, -Z.
constexpr std::array<std::string_view, 6> kFaceFiles = {
    "textures/skybox/vz_sunshine/vz_sunshine_rt.png",  // +X
    "textures/skybox/vz_sunshine/vz_sunshine_lf.png",  // -X
    "textures/skybox/vz_sunshine/vz_sunshine_up.png",  // +Y
    "textures/skybox/vz_sunshine/vz_sunshine_dn.png",  // -Y
    "textures/skybox/vz_sunshine/vz_sunshine_fr.png",  // +Z
    "textures/skybox/vz_sunshine/vz_sunshine_bk.png",  // -Z
};

const std::filesystem::path kSkyboxVertexShaderPath = "shaders/skybox.vs";
const std::filesystem::path kSkyboxFragmentShaderPath = "shaders/skybox.fs";

// Uniform names in assets/shaders/skybox.fs.
constexpr std::string_view kEnvironmentMapUniform = "environmentMap";  // skybox cubemap sampler
constexpr std::string_view kDoGammaUniform = "doGamma";                // apply gamma correction
constexpr std::string_view kVflippedUniform = "vflipped";              // cubemap stored V-flipped

::Texture2D LoadCubemap() {
  std::array<::Image, kFaceFiles.size()> faces{};
  std::transform(kFaceFiles.begin(), kFaceFiles.end(), faces.begin(), [](std::string_view file) {
    return LoadImage(AssetPath(file).c_str());
  });

  const bool any_face_failed = std::any_of(
      faces.begin(), faces.end(), [](const ::Image& face) { return face.width == 0; });
  if (any_face_failed) {
    for (std::size_t i = 0; i < faces.size(); ++i) {
      if (faces[i].width == 0) {
        spdlog::error("[raylib] skybox: cannot load face '{}'", AssetPath(kFaceFiles[i]));
      }
    }
    for (::Image& face : faces) {
      UnloadImage(face);
    }
    return ::Texture2D{};
  }

  const Eigen::Vector2i face_size{faces[0].width, faces[0].height};

  ::Image atlas = GenImageColor(face_size.x() * 6, face_size.y(), BLACK);
  for (std::size_t i = 0; i < faces.size(); ++i) {
    const ::Rectangle src{0.f, 0.f, static_cast<float>(faces[i].width),
                          static_cast<float>(faces[i].height)};
    const ::Rectangle dst{static_cast<float>(i) * static_cast<float>(face_size.x()), 0.f,
                          static_cast<float>(face_size.x()), static_cast<float>(face_size.y())};
    ImageDraw(&atlas, faces[i], src, dst, WHITE);
    UnloadImage(faces[i]);
  }

  ::Texture2D cubemap = LoadTextureCubemap(atlas, CUBEMAP_LAYOUT_LINE_HORIZONTAL);
  UnloadImage(atlas);
  return cubemap;
}

::Shader LoadSkyboxShader() {
  ::Shader shader = LoadShader(AssetPath(kSkyboxVertexShaderPath).c_str(),
                               AssetPath(kSkyboxFragmentShaderPath).c_str());

  if (GetShaderLocation(shader, kEnvironmentMapUniform.data()) < 0) {
    spdlog::error("[raylib] skybox: shader compile failed (no 'environmentMap' uniform)");
    SafeUnloadShader(shader);
    return ::Shader{};
  }

  const int env_map = MATERIAL_MAP_CUBEMAP;
  const int flag_off = 0;
  SetShaderValue(shader, GetShaderLocation(shader, kEnvironmentMapUniform.data()), &env_map,
                 SHADER_UNIFORM_INT);
  SetShaderValue(shader, GetShaderLocation(shader, kDoGammaUniform.data()), &flag_off,
                 SHADER_UNIFORM_INT);
  SetShaderValue(shader, GetShaderLocation(shader, kVflippedUniform.data()), &flag_off,
                 SHADER_UNIFORM_INT);
  return shader;
}

}  // namespace

Skybox LoadSkybox() {
  ::Texture2D cubemap = LoadCubemap();
  if (cubemap.id == 0) {
    return Skybox{};
  }

  ::Shader shader = LoadSkyboxShader();
  if (shader.id == 0) {
    UnloadTexture(cubemap);
    return Skybox{};
  }

  auto res = std::make_shared<SkyboxResources>();
  res->shader = MakeManagedShader(shader);
  res->cubemap = MakeManagedTexture(cubemap);

  ::Model model = LoadModelFromMesh(GenMeshCube(1.f, 1.f, 1.f));
  // vz_sunshine faces are Y-up; rotate into this engine's Z-up world.
  model.transform = MatrixRotateX(90.f * DEG2RAD);
  model.materials[0].shader = *res->shader;
  model.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture = *res->cubemap;

  res->model = std::shared_ptr<::Model>(new ::Model(model), [](::Model* m) {
    // shader/cubemap above are owned independently (and may already be freed);
    // clear the material's references so UnloadModel doesn't touch them.
    if (m->materialCount > 0) {
      m->materials[0].shader.id = 0;
      m->materials[0].maps[MATERIAL_MAP_CUBEMAP].texture.id = 0;
    }
    if (IsGlContextAlive()) {
      UnloadModel(*m);
    }
    delete m;
  });

  spdlog::info("[raylib] skybox loaded ({}x{} per face)", res->cubemap->width, res->cubemap->height);
  return Skybox{.res = std::move(res)};
}

void DrawSkybox(const SkyboxResources& skybox) {
  rlDisableBackfaceCulling();
  rlDisableDepthMask();
  DrawModel(*skybox.model, Vector3Zero(), 1.f, WHITE);
  rlEnableBackfaceCulling();
  rlEnableDepthMask();
}

}  // namespace z13::raylib
