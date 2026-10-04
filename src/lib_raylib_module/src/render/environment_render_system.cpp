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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/pose_smoothing.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/player_color.h>
#include <z13_settings/settings.h>

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

constexpr float kAvatarRadius = 0.5f;
constexpr float kMaxColorChannel = 255.f;

// One directional "sun", Z-up world.
constexpr ::Vector3 kSunPosition{60.f, 40.f, 80.f};
constexpr ::Vector3 kSunTarget{0.f, 0.f, 0.f};

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RaylibCamera, Skybox, RenderModel, AvatarModel, Lighting, BuildingBlock,
                                         z13::VisualSmoothing>(world);
}

// Where everything not the local player's own is drawn, chasing its simulated transform in
// real time so rollback corrections glide in. Own entities are drawn as simulated.
struct DrawnPoses {
  std::unordered_map<flecs::entity_t, z13::math::SmoothedPose> by_entity;
  std::optional<std::chrono::steady_clock::time_point> last_frame;
};

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
  auto loaded = LoadModelFromAsset(kSpaceshipAsset);
  if (!loaded) {
    log_error("[raylib] assimp: {}", loaded.error());
    return RenderModel{};
  }
  for (const std::string& warning : loaded->warnings) {
    log_warn("[raylib] assimp: '{}': {}", kSpaceshipAsset, warning);
  }
  ::Model& model = loaded->model;

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

// UnloadModel frees mesh arrays with RL_FREE, so they must come from MemAlloc.
float* CopyToRaylib(const std::vector<float>& values) {
  auto* data = static_cast<float*>(MemAlloc(static_cast<unsigned int>(values.size() * sizeof(float))));
  std::ranges::copy(values, data);
  return data;
}

// Flat-shaded: every face gets its own three vertices and normal.
::Mesh GenMeshOctahedron(float radius) {
  constexpr uint32_t kFaces = 8;
  constexpr uint32_t kCornersPerFace = 3;
  constexpr uint32_t kTexcoordsPerVertex = 2;
  constexpr uint32_t kVertexCount = kFaces * kCornersPerFace;
  std::vector<float> vertices;
  std::vector<float> normals;
  for (uint32_t face = 0; face < kFaces; ++face) {
    const Eigen::Vector3f sign((face & 1) ? -1.f : 1.f, (face & 2) ? -1.f : 1.f, (face & 4) ? -1.f : 1.f);
    std::array<Eigen::Vector3f, kCornersPerFace> corners = {
        Eigen::Vector3f(sign.x() * radius, 0.f, 0.f),
        Eigen::Vector3f(0.f, sign.y() * radius, 0.f),
        Eigen::Vector3f(0.f, 0.f, sign.z() * radius),
    };
    if (sign.prod() < 0.f) {
      std::swap(corners[1], corners[2]);  // keep the winding counter-clockwise from outside
    }
    const Eigen::Vector3f normal = sign.normalized();
    for (const Eigen::Vector3f& corner : corners) {
      vertices.insert(vertices.end(), {corner.x(), corner.y(), corner.z()});
      normals.insert(normals.end(), {normal.x(), normal.y(), normal.z()});
    }
  }

  ::Mesh mesh{};
  mesh.vertexCount = static_cast<int>(kVertexCount);
  mesh.triangleCount = static_cast<int>(kFaces);
  mesh.vertices = CopyToRaylib(vertices);
  mesh.normals = CopyToRaylib(normals);
  mesh.texcoords = CopyToRaylib(std::vector<float>(kVertexCount * kTexcoordsPerVertex));
  UploadMesh(&mesh, false);
  return mesh;
}

AvatarModel LoadAvatar(const Lighting& lighting) {
  ::Model model = LoadModelFromMesh(GenMeshOctahedron(kAvatarRadius));
  const unsigned int borrowed_shader_id = ApplyLightingShader(model, lighting);
  auto res = std::make_shared<ModelResources>();
  res->model = MakeManagedModel(model, borrowed_shader_id);
  return AvatarModel{.res = std::move(res)};
}

::Color ToRaylibColor(const z13::gameplay::Rgb& rgb) {
  const auto channel = [](float value) { return static_cast<unsigned char>(value * kMaxColorChannel); };
  return {channel(rgb[0]), channel(rgb[1]), channel(rgb[2]), static_cast<unsigned char>(kMaxColorChannel)};
}

using RemotePlayerQuery = flecs::query<const gameplay::Player, const Eigen::Matrix4f>;
using DrawnQuery = flecs::query<const Eigen::Matrix4f>;

bool IsOwn(flecs::entity e) {
  for (flecs::entity current = e; current; current = current.parent()) {
    if (current.has<z13::input::CurrentActionListenerTag>()) {
      return true;
    }
  }
  return false;
}

// Rebuilt every frame, so entities that are gone or became own drop out.
void ChaseDrawnPoses(DrawnPoses& drawn, const VisualSmoothing& settings, const DrawnQuery& drawn_query) {
  const auto now = std::chrono::steady_clock::now();
  const float delta_seconds =
      drawn.last_frame ? std::chrono::duration<float>(now - *drawn.last_frame).count() : 0.f;
  drawn.last_frame = now;
  const z13::math::PoseSmoothingParams params {
      .smooth_time_seconds = settings.smooth_time_seconds,
      .snap_distance = settings.snap_distance,
      .snap_angle_rad = z13::math::ToRadians(settings.snap_angle_deg),
  };

  std::unordered_map<flecs::entity_t, z13::math::SmoothedPose> next;
  drawn_query.each([&](flecs::entity e, const Eigen::Matrix4f& transform) {
    if (IsOwn(e)) {
      return;
    }
    const auto previous = drawn.by_entity.find(e.id());
    z13::math::SmoothedPose pose =
        previous != drawn.by_entity.end() ? previous->second : z13::math::PoseAt(transform);
    z13::math::ChasePose(pose, transform, delta_seconds, params);
    next.emplace(e.id(), pose);
  });
  drawn.by_entity = std::move(next);
}

Eigen::Matrix4f DrawnTransform(const DrawnPoses& drawn, flecs::entity e, const Eigen::Matrix4f& transform) {
  const auto found = drawn.by_entity.find(e.id());
  return found != drawn.by_entity.end() ? z13::math::DrawnTransform(found->second, transform) : transform;
}

// Every avatar shares the one model; only its transform and tint change per player.
void DrawRemotePlayers(const flecs::world& world, const RemotePlayerQuery& remote_players, const DrawnPoses& drawn) {
  if (!world.has<AvatarModel>()) {
    return;
  }
  const AvatarModel& avatar = world.get<AvatarModel>();
  if (!avatar.res || avatar.res->model->meshCount == 0) {
    return;
  }
  remote_players.each([&avatar, &drawn](flecs::entity e, const gameplay::Player& player, const Eigen::Matrix4f& transform) {
    avatar.res->model->transform = EigenToRaylibMatrix(DrawnTransform(drawn, e, transform));
    DrawModel(*avatar.res->model, Vector3Zero(), 1.f, ToRaylibColor(z13::gameplay::PlayerColor(player.id)));
  });
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

// Only the local player's camera renders; other players' Cameras arrive with snapshots.
void ReleaseOrphanRaylibCamera(flecs::entity e, const RaylibCamera&) {
  if (!e.has<gameplay::Camera>() || !e.has<z13::input::CurrentActionListenerTag>()) {
    e.remove<RaylibCamera>();
  }
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
  // The launcher overwrites this with the loaded settings (z13::InstallSettings).
  world.set<z13::VisualSmoothing>({});
  // Shared by the closures below; lives as long as the world.
  auto block_models = std::make_shared<BlockModels>();
  auto drawn_poses = std::make_shared<DrawnPoses>();

  // RaylibData is set right after InitWindow, so a live GL context is guaranteed.
  world.observer<RaylibData>("EnvironmentRenderSystem::LoadEnvironment")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world](RaylibData&) {
        Lighting lighting = LoadLighting();
        world.set<RenderModel>(LoadSpaceship(lighting));
        world.set<AvatarModel>(LoadAvatar(lighting));
        world.set<Lighting>(std::move(lighting));
        world.set<Skybox>(LoadSkybox());
      });

  // Everything below runs in the Render phase, ahead of Draw (systems in one phase run
  // in registration order), so the scene is synced with this frame's final state.
  world.system<const RaylibData, const gameplay::Camera>("EnvironmentRenderSystem::EnsureRaylibCamera")
      .kind<Render>()
      .tick_source<RenderGate>()
      .with<z13::input::CurrentActionListenerTag>()
      .without<RaylibCamera>()
      .write<RaylibCamera>()
      .each(EnsureRaylibCamera);

  world.system<RaylibCamera, const gameplay::Camera, const Eigen::Matrix4f>(
           "EnvironmentRenderSystem::SyncRaylibCamera")
      .kind<Render>()
      .tick_source<RenderGate>()
      .with<z13::input::CurrentActionListenerTag>()
      .each(SyncRaylibCamera);

  world.system<const RaylibCamera>("EnvironmentRenderSystem::ReleaseOrphanRaylibCamera")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<gameplay::Camera>()
      .read<z13::input::CurrentActionListenerTag>()
      .write<RaylibCamera>()
      .each(ReleaseOrphanRaylibCamera);

  // Singleton-only term, so $this is empty: use the iter/row overload.
  world.system<const Lighting>("EnvironmentRenderSystem::ReleaseOrphanModels")
      .kind<Render>()
      .tick_source<RenderGate>()
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
      .tick_source<RenderGate>()
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
      .tick_source<RenderGate>()
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

  RemotePlayerQuery remote_player_query =
      world.query_builder<const gameplay::Player, const Eigen::Matrix4f>("EnvironmentRenderSystem::RemotePlayerQuery")
          .without<z13::input::CurrentActionListenerTag>()
          .build();

  DrawnQuery drawn_query = world.query_builder<const Eigen::Matrix4f>("EnvironmentRenderSystem::DrawnQuery")
                               .with<gameplay::Player>()
                               .or_()
                               .with<BuildingBlock>()
                               .build();

  world.system<const VisualSmoothing>("EnvironmentRenderSystem::ChaseDrawnPoses")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::input::CurrentActionListenerTag>()
      .each([drawn_poses, drawn_query](flecs::iter&, size_t, const VisualSmoothing& settings) {
        ChaseDrawnPoses(*drawn_poses, settings, drawn_query);
      });

  // Render phase: 3D scene between FrameBegin (PreRender) and FrameEnd (FinalizeRender).
  world.system<const RaylibCamera, const WindowSize>("EnvironmentRenderSystem::Draw")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<BuildingBlock>()
      .each([world, block_query, block_models, remote_player_query, drawn_poses](
                const RaylibCamera& raylib_camera, const WindowSize& size) {
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
            [block_models, &drawn_poses](flecs::entity e, const BuildingBlock& block, const Eigen::Matrix4f& transform) {
              const auto it = block_models->find(e.id());
              if (it != block_models->end() && it->second.model->meshCount > 0) {
                it->second.model->transform = EigenToRaylibMatrix(DrawnTransform(*drawn_poses, e, transform));
                DrawModel(*it->second.model, Vector3Zero(), 1.f, block.color);
              }
            });
        DrawRemotePlayers(world, remote_player_query, *drawn_poses);

        EndScene3D();
      });
}

}  // namespace

void EnvironmentRenderSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::raylib
