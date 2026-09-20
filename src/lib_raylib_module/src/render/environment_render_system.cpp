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

#include "environment_render_system.h"

#include <array>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <Eigen/Dense>
#include <flecs.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <lib_core/components.h>
#include <lib_core/log.h>
#include <lib_core/math.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>

#include <raylib_module/raylib_components.h>

#include "../tools/assimp_loader.h"
#include "../tools/math_convert.h"
#include "lights.h"
#include "render_components.h"
#include "render_resources.h"
#include "skybox.h"

namespace z13::raylib {

namespace {

constexpr std::string_view kSpaceshipAsset = "models/spaceship2/spaceship.fbx";
constexpr float kSpaceshipScale = 0.1f;
constexpr ::Vector3 kSpaceshipPosition{30.f, 0.f, 0.f};
constexpr float kSpaceshipPitchDegrees = 90.f;  // about world +X (FBX Y-up -> Z-up)
constexpr float kSpaceshipYawDegrees = -90.f;   // about world +Y (art orientation)

constexpr ::Color kPlacedBlockColor = GREEN;

// One directional "sun", Z-up world.
constexpr ::Vector3 kSunPosition{60.f, 40.f, 80.f};
constexpr ::Vector3 kSunTarget{0.f, 0.f, 0.f};

void RegisterComponents(flecs::world world) {
  world.component<RaylibCamera>();
  world.component<Skybox>().add(flecs::Singleton);
  world.component<RenderModel>().add(flecs::Singleton);
  world.component<Lighting>().add(flecs::Singleton);
  world.component<BuildingBlock>();
}

// Per-entity cube models backing placed blocks / the brush preview, keyed by
// entity -- the model itself doesn't live on BuildingBlock (see render_components.h).
using BlockModels = std::unordered_map<flecs::entity_t, ModelResources>;

// Points every material of `model` at Lighting's shared shader, and returns
// the id MakeManagedModel needs to skip unloading a shader it doesn't own.
unsigned int ApplyLightingShader(::Model& model, const Lighting& lighting) {
  if (!lighting.res) {
    return 0;
  }
  for (int i = 0; i < model.materialCount; ++i) {
    model.materials[i].shader = *lighting.res->shader;
  }
  return lighting.res->shader->id;
}

void AddBlockModel(
    BlockModels& block_models, flecs::entity e, const Eigen::Matrix4f& transform, ::Color color,
    const Lighting& lighting) {
  ::Model model = LoadModelFromMesh(GenMeshCube(
      z13::building::kBlockSize, z13::building::kBlockSize, z13::building::kBlockSize));
  model.transform = EigenToRaylibMatrix(transform);
  const unsigned int borrowed_shader_id = ApplyLightingShader(model, lighting);
  block_models[e.id()] = ModelResources{.model = MakeManagedModel(model, borrowed_shader_id)};
  e.set<BuildingBlock>({.color = color});
}

Lighting LoadLighting() {
  ::Shader shader = LoadLightingShader();
  if (shader.id == 0) {
    return Lighting{};
  }

  auto res = std::make_shared<LightingResources>();
  res->shader = MakeManagedShader(shader);

  Light sun{};
  sun.kind = LightKind::Directional;
  sun.position = kSunPosition;
  sun.target = kSunTarget;
  sun.color = WHITE;
  SetupLight(sun, *res->shader, 0);

  return Lighting{.res = std::move(res)};
}

RenderModel LoadSpaceship(const Lighting& lighting) {
  ::Model model = LoadModelFromAsset(kSpaceshipAsset);
  if (model.meshCount == 0) {
    return RenderModel{};
  }

  const unsigned int borrowed_shader_id = ApplyLightingShader(model, lighting);

  Eigen::Affine3f transform = Eigen::Affine3f::Identity();
  transform.translate(
      Eigen::Vector3f(kSpaceshipPosition.x, kSpaceshipPosition.y, kSpaceshipPosition.z));
  transform.rotate(
      Eigen::AngleAxisf(z13::math::ToRadians(kSpaceshipPitchDegrees), Eigen::Vector3f::UnitX()));
  transform.rotate(
      Eigen::AngleAxisf(z13::math::ToRadians(kSpaceshipYawDegrees), Eigen::Vector3f::UnitY()));
  transform.scale(kSpaceshipScale);
  model.transform = EigenToRaylibMatrix(Eigen::Matrix4f(transform.matrix()));

  auto res = std::make_shared<ModelResources>();
  res->model = MakeManagedModel(model, borrowed_shader_id);
  return RenderModel{.res = std::move(res)};
}

// Cameras are derived from gameplay::Camera components each frame, not from add
// events, so restored or edited state is picked up too.
void EnsureRaylibCamera(flecs::entity e, const RaylibData&, const gameplay::Camera& camera) {
  ::Camera3D cam{};
  cam.fovy = camera.fov;
  cam.projection = CAMERA_PERSPECTIVE;
  cam.position = {0.f, 0.f, 0.f};
  cam.target = {1.f, 0.f, 0.f};
  cam.up = {0.f, 0.f, 1.f};

  if (const auto* transform = e.try_get<Eigen::Matrix4f>()) {
    UpdateCameraFromTransform(cam, *transform);
  }

  e.set<RaylibCamera>({.camera = cam});
  log_info("RaylibCamera created for '{}' (fov {})", e.name().c_str(), camera.fov);
}

void SyncRaylibCamera(
    RaylibCamera& raylib_camera, const gameplay::Camera& camera, const Eigen::Matrix4f& transform) {
  raylib_camera.camera.fovy = camera.fov;
  UpdateCameraFromTransform(raylib_camera.camera, transform);
}

void ReleaseOrphanRaylibCamera(flecs::entity e, const RaylibCamera&) {
  e.remove<RaylibCamera>();
}

// Drops the model of every entity that is gone or is no longer a block/brush.
void ReleaseOrphanModels(const flecs::world& world, BlockModels& block_models) {
  std::erase_if(block_models, [&world](const auto& entry) {
    const flecs::entity_t id = entry.first;
    if (!world.is_alive(id)) {
      return true;
    }

    const flecs::entity e = world.entity(id);
    if (e.has<z13::building::BasicBlock>() || e.has<z13::building::Brush>()) {
      return false;
    }
    e.remove<BuildingBlock>();
    return true;
  });
}

// Replacement for BeginMode3D/EndMode3D: those need CORE for the aspect ratio,
// which does not exist without InitWindow. Feed rlgl's matrices directly.
void BeginScene3D(const ::Camera3D& camera, const Eigen::Vector2i& size) {
  rlDrawRenderBatchActive();
  const float aspect =
      size.y() > 0 ? static_cast<float>(size.x()) / static_cast<float>(size.y()) : 1.f;
  const ::Matrix projection = MatrixPerspective(camera.fovy * DEG2RAD, aspect,
                                                rlGetCullDistanceNear(), rlGetCullDistanceFar());
  rlSetMatrixProjection(projection);
  rlSetMatrixModelview(MatrixLookAt(camera.position, camera.target, camera.up));
  rlEnableDepthTest();
}

void EndScene3D() {
  rlDrawRenderBatchActive();
  rlDisableDepthTest();
}

void RegisterSystems(flecs::world world) {
  // Shared by the closures below; lives as long as the world.
  auto block_models = std::make_shared<BlockModels>();

  // RaylibData is set right after InitWindow, so a live GL context is guaranteed.
  world.observer<RaylibData>("EnvironmentRenderSystem::LoadEnvironment")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world](RaylibData&) {
        Lighting lighting = LoadLighting();
        world.set<RenderModel>(LoadSpaceship(lighting));
        world.set<Lighting>(std::move(lighting));
        world.set<Skybox>(LoadSkybox());
      });

  // Everything below runs in the Render phase, ahead of Draw (systems in one phase run
  // in registration order), so the scene is synced with this frame's final state.
  world.system<const RaylibData, const gameplay::Camera>("EnvironmentRenderSystem::EnsureRaylibCamera")
      .kind<Render>()
      .without<RaylibCamera>()
      .write<RaylibCamera>()
      .each(EnsureRaylibCamera);

  world.system<RaylibCamera, const gameplay::Camera, const Eigen::Matrix4f>(
           "EnvironmentRenderSystem::SyncRaylibCamera")
      .kind<Render>()
      .each(SyncRaylibCamera);

  world.system<const RaylibCamera>("EnvironmentRenderSystem::ReleaseOrphanRaylibCamera")
      .kind<Render>()
      .without<gameplay::Camera>()
      .write<RaylibCamera>()
      .each(ReleaseOrphanRaylibCamera);

  // Singleton-only term, so $this is empty: use the iter/row overload.
  world.system<const Lighting>("EnvironmentRenderSystem::ReleaseOrphanModels")
      .kind<Render>()
      .read<z13::building::BasicBlock>()
      .read<z13::building::Brush>()
      .write<BuildingBlock>()
      .each([block_models](flecs::iter& it, size_t, const Lighting&) {
        ReleaseOrphanModels(it.world(), *block_models);
      });

  // The brush preview and placed blocks share one cube model; the transform is
  // refreshed at draw time from the entity's matrix.
  world.system<const z13::building::Brush, const Eigen::Matrix4f, const Lighting>(
           "EnvironmentRenderSystem::AddBrushModel")
      .kind<Render>()
      .without<BuildingBlock>()
      .write<BuildingBlock>()
      .each([block_models](
                flecs::entity e, const z13::building::Brush&, const Eigen::Matrix4f& transform,
                const Lighting& lighting) {
        AddBlockModel(*block_models, e, transform, WHITE, lighting);
      });

  world.system<const z13::building::BasicBlock, const Eigen::Matrix4f, const Lighting>(
           "EnvironmentRenderSystem::AddBlockModel")
      .kind<Render>()
      .without<BuildingBlock>()
      .write<BuildingBlock>()
      .each([block_models](
                flecs::entity e, const z13::building::BasicBlock&, const Eigen::Matrix4f& transform,
                const Lighting& lighting) {
        AddBlockModel(*block_models, e, transform, kPlacedBlockColor, lighting);
      });

  flecs::query<const BuildingBlock, const Eigen::Matrix4f> block_query =
      world.query_builder<const BuildingBlock, const Eigen::Matrix4f>("EnvironmentRenderSystem::BlockQuery")
          .build();

  // Render phase: 3D scene between FrameBegin (PreRender) and FrameEnd (FinalizeRender).
  world.system<const RaylibCamera, const WindowSize>("EnvironmentRenderSystem::Draw")
      .kind<Render>()
      .read<BuildingBlock>()
      .each([world, block_query, block_models](const RaylibCamera& raylib_camera, const WindowSize& size) {
        if (world.has<Lighting>()) {
          const Lighting& lighting = world.get<Lighting>();
          if (lighting.res) {
            const ::Vector3& eye = raylib_camera.camera.position;
            const std::array<float, 3> view_pos = {eye.x, eye.y, eye.z};
            SetShaderValue(*lighting.res->shader,
                           lighting.res->shader->locs[SHADER_LOC_VECTOR_VIEW], view_pos.data(),
                           SHADER_UNIFORM_VEC3);
          }
        }

        BeginScene3D(raylib_camera.camera, size.size);

        if (world.has<Skybox>()) {
          const Skybox& skybox = world.get<Skybox>();
          if (skybox.res) {
            DrawSkybox(*skybox.res);
          }
        }

        if (world.has<RenderModel>()) {
          const RenderModel& render_model = world.get<RenderModel>();
          if (render_model.res) {
            DrawModel(*render_model.res->model, Vector3Zero(), 1.f, WHITE);
          }
        }

        block_query.each(
            [block_models](flecs::entity e, const BuildingBlock& block, const Eigen::Matrix4f& transform) {
              const auto it = block_models->find(e.id());
              if (it != block_models->end() && it->second.model->meshCount > 0) {
                it->second.model->transform = EigenToRaylibMatrix(transform);
                DrawModel(*it->second.model, Vector3Zero(), 1.f, block.color);
              }
            });

        EndScene3D();
      });
}

}  // namespace

void EnvironmentRenderSystem::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<InitSystemsEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });
}

}  // namespace z13::raylib
