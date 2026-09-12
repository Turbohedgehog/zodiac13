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

#include <array>
#include <cstddef>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <spdlog/spdlog.h>

#include "../tools/asset_path.h"

namespace z13::raylib {

namespace {

// CUBEMAP_LAYOUT_LINE_HORIZONTAL face order: +X, -X, +Y, -Y, +Z, -Z.
constexpr std::array<const char*, 6> kFaceFiles = {
    "textures/skybox/vz_sunshine/vz_sunshine_rt.png",  // +X
    "textures/skybox/vz_sunshine/vz_sunshine_lf.png",  // -X
    "textures/skybox/vz_sunshine/vz_sunshine_up.png",  // +Y
    "textures/skybox/vz_sunshine/vz_sunshine_dn.png",  // -Y
    "textures/skybox/vz_sunshine/vz_sunshine_fr.png",  // +Z
    "textures/skybox/vz_sunshine/vz_sunshine_bk.png",  // -Z
};

::Texture2D LoadCubemap() {
  std::array<::Image, 6> faces{};
  for (std::size_t i = 0; i < faces.size(); ++i) {
    faces[i] = LoadImage(AssetPath(kFaceFiles[i]).c_str());
  }

  if (faces[0].width == 0) {
    spdlog::error("[raylib] skybox: cannot load face '{}'", AssetPath(kFaceFiles[0]));
    for (::Image& face : faces) UnloadImage(face);
    return ::Texture2D{};
  }

  const int face_w = faces[0].width;
  const int face_h = faces[0].height;

  ::Image atlas = GenImageColor(face_w * 6, face_h, BLACK);
  for (std::size_t i = 0; i < faces.size(); ++i) {
    const ::Rectangle src{0.f, 0.f, static_cast<float>(faces[i].width),
                          static_cast<float>(faces[i].height)};
    const ::Rectangle dst{static_cast<float>(i) * static_cast<float>(face_w), 0.f,
                          static_cast<float>(face_w), static_cast<float>(face_h)};
    ImageDraw(&atlas, faces[i], src, dst, WHITE);
    UnloadImage(faces[i]);
  }

  ::Texture2D cubemap = LoadTextureCubemap(atlas, CUBEMAP_LAYOUT_LINE_HORIZONTAL);
  UnloadImage(atlas);
  return cubemap;
}

::Shader LoadSkyboxShader() {
  ::Shader shader = LoadShader(AssetPath("shaders/skybox.vs").c_str(),
                               AssetPath("shaders/skybox.fs").c_str());

  if (GetShaderLocation(shader, "environmentMap") < 0) {
    spdlog::error("[raylib] skybox: shader compile failed (no 'environmentMap' uniform)");
    SafeUnloadShader(shader);
    return ::Shader{};
  }

  const int env_map = MATERIAL_MAP_CUBEMAP;
  const int flag_off = 0;
  SetShaderValue(shader, GetShaderLocation(shader, "environmentMap"), &env_map, SHADER_UNIFORM_INT);
  SetShaderValue(shader, GetShaderLocation(shader, "doGamma"), &flag_off, SHADER_UNIFORM_INT);
  SetShaderValue(shader, GetShaderLocation(shader, "vflipped"), &flag_off, SHADER_UNIFORM_INT);
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
  res->cubemap = cubemap;
  res->shader = shader;
  res->model = LoadModelFromMesh(GenMeshCube(1.f, 1.f, 1.f));
  // vz_sunshine faces are Y-up; rotate into this engine's Z-up world (mirrors
  // the +90deg X quaternion the old Ogre setSkyBox applied).
  res->model.transform = MatrixRotateX(90.f * DEG2RAD);
  res->model.materials[0].shader = res->shader;
  res->model.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture = res->cubemap;

  spdlog::info("[raylib] skybox loaded ({}x{} per face)", res->cubemap.width, res->cubemap.height);
  return Skybox{.res = std::move(res)};
}

void DrawSkybox(const SkyboxResources& skybox) {
  rlDisableBackfaceCulling();
  rlDisableDepthMask();
  DrawModel(skybox.model, ::Vector3{0.f, 0.f, 0.f}, 1.f, WHITE);
  rlEnableBackfaceCulling();
  rlEnableDepthMask();
}

}  // namespace z13::raylib
