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
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/frustum.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/drawn_poses.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/player_color.h>
#include <z13/components/station.h>
#include <z13_primitives/chunk_mesh.h>
#include <z13_primitives/draw_order.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>
#include <z13_settings/settings.h>

#include <raylib_module/raylib_components.h>

#include "../tools/assimp_loader.h"
#include "../tools/math_convert.h"
#include "block_checker.h"
#include "block_chunks.h"
#include "block_meshes.h"
#include "lights.h"
#include "render_components.h"
#include "render_resources.h"
#include "render_stats.h"
#include "skybox.h"

namespace z13::raylib {

namespace {

constexpr std::string_view kSpaceshipAsset = "models/spaceship2/spaceship.fbx";
constexpr float kSpaceshipScale = 0.1f;
constexpr ::Vector3 kSpaceshipPosition{30.f, 0.f, 0.f};
constexpr float kSpaceshipPitchDegrees = 90.f;  // about world +X (FBX Y-up -> Z-up)
constexpr float kSpaceshipYawDegrees = -90.f;   // about world +Y (art orientation)

constexpr ::Color kBrushPreviewTint {255, 255, 255, 128};
constexpr ::Color kValidBuildTint {120, 255, 120, 128};
constexpr ::Color kRefusedBuildTint {255, 90, 90, 128};
// A cut that would be accepted: the box it takes, in orange.
constexpr ::Color kValidCutTint {255, 170, 60, 128};

constexpr float kAvatarRadius = 0.5f;
constexpr float kMaxColorChannel = 255.f;

// One directional "sun", Z-up world.
constexpr ::Vector3 kSunPosition{60.f, 40.f, 80.f};
constexpr ::Vector3 kSunTarget{0.f, 0.f, 0.f};

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RaylibCamera, Skybox, RenderModel, AvatarModel, Lighting, BlockMeshes,
                                         BlockChunks, RenderStats, z13::VisualSmoothing, z13::RenderTuning>(world);
}

// Everything not the local player's own chases its simulated transform in real time, so
// rollback corrections glide in.
struct SmoothedDrawing {
  z13::DrawnPoses poses;
  std::optional<std::chrono::steady_clock::time_point> last_frame;
};

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

Lighting LoadLighting() {
  ::Shader shader = LoadLightingShader();
  if (shader.id == 0) {
    return Lighting{};
  }

  auto res = std::make_shared<LightingResources>();
  res->shader = MakeManagedShader(shader);
  res->checker = SetupCheckerUniforms(shader);

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

// The ship belongs to the ordinary scene alone, so a station game never parses its model.
// Waits out a pending restore: a joining client has Gameplay a frame before Welcome's mode.
void ReconcileSpaceship(flecs::world world, const Lighting& lighting) {
  if (z13::flecs_tools::IsCatchingUp(world)) {
    return;
  }
  const bool wanted = world.has<gameplay::Gameplay>() && !world.has<z13::station::StationMode>();
  if (wanted && !world.has<RenderModel>()) {
    world.set<RenderModel>(LoadSpaceship(lighting));
  } else if (!wanted && world.has<RenderModel>()) {
    world.remove<RenderModel>();
  }
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

void ChaseDrawnPoses(SmoothedDrawing& drawing, const VisualSmoothing& settings, const DrawnQuery& drawn_query) {
  const auto now = std::chrono::steady_clock::now();
  const float delta_seconds =
      drawing.last_frame ? std::chrono::duration<float>(now - *drawing.last_frame).count() : 0.f;
  drawing.last_frame = now;
  const z13::math::PoseSmoothingParams params {
      .smooth_time_seconds = settings.smooth_time_seconds,
      .snap_distance = settings.snap_distance,
      .snap_angle_rad = z13::math::ToRadians(settings.snap_angle_deg),
  };
  z13::ChaseDrawnPoses(
      drawing.poses, drawn_query, delta_seconds, params, z13::HasInAncestry<z13::input::CurrentActionListenerTag>);
}

// Every avatar shares the one model; only its transform and tint change per player.
void DrawRemotePlayers(const flecs::world& world, const RemotePlayerQuery& remote_players, const z13::DrawnPoses& drawn) {
  if (!world.has<AvatarModel>()) {
    return;
  }
  const AvatarModel& avatar = world.get<AvatarModel>();
  if (!avatar.res || avatar.res->model->meshCount == 0) {
    return;
  }
  remote_players.each([&avatar, &drawn](flecs::entity e, const gameplay::Player& player, const Eigen::Matrix4f& transform) {
    avatar.res->model->transform = EigenToRaylibMatrix(z13::DrawnTransform(drawn, e, transform));
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

using BlockQuery = flecs::query<const z13::station::Block>;
using BrushQuery = flecs::query<const z13::building::Brush, const Eigen::Matrix4f>;

// Blocks drawn one by one, past the chunks: glass and previews.
struct SingleBlockDrawing {
  std::reference_wrapper<BlockMeshes> meshes;
  std::reference_wrapper<const Lighting> lighting;
  BlockMeshes::OptionalPalette palette;
};

void DrawBlock(const SingleBlockDrawing& drawing, const z13::station::Block& block, ::Color tint) {
  const std::shared_ptr<::Model> model =
      drawing.meshes.get().Get({.type_id = block.spec.type_id, .size = block.spec.size}, drawing.palette);
  if (model->meshCount == 0) {
    return;
  }
  UseChecker(drawing.lighting, z13::building::primitives::MaterialOf(drawing.palette, block.spec.type_id));
  const Eigen::Isometry3f pose =
      z13::building::primitives::WorldPose(z13::building::primitives::PoseOf(block), z13::station::kCellSize);
  model->transform = EigenToRaylibMatrix(Eigen::Matrix4f(pose.matrix()));
  DrawModel(*model, Vector3Zero(), 1.f, tint);
}

// What a build would place: in station mode the brush's BrushPreview, green if it would
// be accepted, else the ship scene's cube, snapped around the brush as the build snaps it.
void DrawBrushPreview(
    const flecs::world& world, flecs::entity brush, const Eigen::Matrix4f& transform,
    const SingleBlockDrawing& drawing) {
  if (world.has<z13::station::StationMode>()) {
    if (brush.has<z13::station::BrushPreview>()) {
      const auto& preview = brush.get<z13::station::BrushPreview>();
      const ::Color valid_tint =
          preview.kind == z13::station::BrushPreview::Kind::kBuild ? kValidBuildTint : kValidCutTint;
      DrawBlock(drawing, preview.block, preview.valid ? valid_tint : kRefusedBuildTint);
    }
    return;
  }
  const Eigen::Vector3f point = z13::math::ExtractTranslation<float>(transform) / z13::station::kCellSize;
  DrawBlock(drawing, z13::building::primitives::PlaceCentredOn(point, z13::station::CubeSpec()), kBrushPreviewTint);
}

BlockMeshes::OptionalPalette PaletteOf(const flecs::world& world) {
  const auto* palette = world.try_get<z13::building::primitives::BlockPalette>();
  return palette != nullptr ? BlockMeshes::OptionalPalette(palette->palette) : std::nullopt;
}

std::vector<z13::station::Block> CollectBlocks(const BlockQuery& query) {
  std::vector<z13::station::Block> blocks;
  query.each([&blocks](const z13::station::Block& block) { blocks.push_back(block); });
  return blocks;
}

// Rebuilds the chunks only when blocks, the chunk size or the palette changed.
void SyncBlockChunks(BlockChunks& chunks, const BlockQuery& blocks, const z13::RenderTuning& tuning,
                     BlockMeshes::OptionalPalette palette) {
  // Checked first: iterating the query resets its changed state.
  if (blocks.changed() || !chunks.SyncedWith(tuning.chunk_cells, palette)) {
    chunks.Sync(CollectBlocks(blocks), palette, tuning.chunk_cells);
  }
}

Eigen::AlignedBox3f BoundsInMeters(const z13::station::Block& block) {
  const z13::building::primitives::CellBox cells = z13::building::primitives::OccupiedCells(block);
  return {cells.min.cast<float>() * z13::station::kCellSize, cells.End().cast<float>() * z13::station::kCellSize};
}

std::vector<z13::station::Block> VisibleBlocks(
    std::span<const z13::station::Block> blocks, const z13::math::Frustum& frustum) {
  std::vector<z13::station::Block> visible;
  std::ranges::copy_if(blocks, std::back_inserter(visible), [&frustum](const z13::station::Block& block) {
    return frustum.Intersects(BoundsInMeters(block));
  });
  return visible;
}

// The view-projection rlgl draws with, set by BeginScene3D.
z13::math::Frustum CurrentFrustum() {
  return z13::math::Frustum(RaylibToEigenMatrix(rlGetMatrixProjection()) * RaylibToEigenMatrix(rlGetMatrixModelview()));
}

// Players and opaque chunks first; then glass from the farthest and previews, blended over
// them and without writing depth, so nothing behind them is hidden.
void DrawScene(const flecs::world& world, const BrushQuery& brushes, const RemotePlayerQuery& remote_players,
               const z13::DrawnPoses& drawn, const ::Vector3& eye, RenderStats& stats) {
  stats = {};
  DrawRemotePlayers(world, remote_players, drawn);
  if (!world.has<BlockMeshes>() || !world.has<BlockChunks>() || !world.has<Lighting>()) {
    return;
  }
  const Lighting& lighting = world.get<Lighting>();
  const SingleBlockDrawing drawing {
      .meshes = world.get_mut<BlockMeshes>(), .lighting = lighting, .palette = PaletteOf(world)};
  const BlockChunks& chunks = world.get<BlockChunks>();
  const z13::math::Frustum frustum = CurrentFrustum();
  chunks.DrawOpaque(frustum, lighting, stats);

  rlDrawRenderBatchActive();
  rlDisableDepthMask();
  const std::vector<z13::station::Block> glass = VisibleBlocks(chunks.Transparent(), frustum);
  stats.glass = chunks.Transparent().size();
  stats.glass_drawn = glass.size();
  const z13::building::primitives::DrawOrder order = z13::building::primitives::SortForDrawing(
      glass, drawing.palette, Eigen::Vector3f(eye.x, eye.y, eye.z) / z13::station::kCellSize);
  for (const size_t i : order.transparent) {
    DrawBlock(drawing, glass[i], WHITE);
  }
  brushes.each([&world, &drawing](flecs::entity brush, const z13::building::Brush&, const Eigen::Matrix4f& transform) {
    DrawBrushPreview(world, brush, transform, drawing);
  });
  rlDrawRenderBatchActive();
  rlEnableDepthMask();
  StopChecker(lighting);
  drawing.meshes.get().ReleaseUnused();
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
  world.set<z13::RenderTuning>({});
  world.set<RenderStats>({});
  // Shared by the closures below; lives as long as the world.
  auto drawing = std::make_shared<SmoothedDrawing>();

  // RaylibData is set right after InitWindow, so a live GL context is guaranteed.
  world.observer<RaylibData>("EnvironmentRenderSystem::LoadEnvironment")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world](RaylibData&) {
        Lighting lighting = LoadLighting();
        world.set<AvatarModel>(LoadAvatar(lighting));
        const ::Shader shader = lighting.res ? *lighting.res->shader : ::Shader {};
        world.set<BlockMeshes>(BlockMeshes(shader));
        world.set<BlockChunks>(BlockChunks(shader));
        world.set<Lighting>(std::move(lighting));
        world.set<Skybox>(LoadSkybox());
      });

  // Everything below runs in the Render phase, ahead of Draw (systems in one phase run
  // in registration order), so the scene is synced with this frame's final state.
  world.system<const Lighting>("EnvironmentRenderSystem::ReconcileSpaceship")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<gameplay::Gameplay>()
      .read<z13::station::StationMode>()
      .write<RenderModel>()
      .each([](flecs::iter& it, size_t, const Lighting& lighting) { ReconcileSpaceship(it.world(), lighting); });

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

  const BlockQuery block_query =
      world.query_builder<const z13::station::Block>("EnvironmentRenderSystem::BlockQuery").detect_changes().build();
  const BrushQuery brush_query =
      world.query_builder<const z13::building::Brush, const Eigen::Matrix4f>("EnvironmentRenderSystem::BrushQuery")
          .build();

  RemotePlayerQuery remote_player_query =
      world.query_builder<const gameplay::Player, const Eigen::Matrix4f>("EnvironmentRenderSystem::RemotePlayerQuery")
          .without<z13::input::CurrentActionListenerTag>()
          .build();

  DrawnQuery drawn_query = world.query_builder<const Eigen::Matrix4f>("EnvironmentRenderSystem::DrawnQuery")
                               .with<gameplay::Player>()
                               .build();

  world.system<const VisualSmoothing>("EnvironmentRenderSystem::ChaseDrawnPoses")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::input::CurrentActionListenerTag>()
      .each([drawing, drawn_query](flecs::iter&, size_t, const VisualSmoothing& settings) {
        ChaseDrawnPoses(*drawing, settings, drawn_query);
      });

  world.system<BlockChunks, const z13::RenderTuning, const z13::building::primitives::BlockPalette*>(
           "EnvironmentRenderSystem::SyncBlockChunks")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::station::Block>()
      .each([block_query](BlockChunks& chunks, const z13::RenderTuning& tuning,
                          const z13::building::primitives::BlockPalette* palette) {
        SyncBlockChunks(chunks, block_query, tuning,
                        palette != nullptr ? BlockMeshes::OptionalPalette(palette->palette) : std::nullopt);
      });

  // Render phase: 3D scene between FrameBegin (PreRender) and FrameEnd (FinalizeRender).
  world.system<const RaylibCamera, const WindowSize, RenderStats>("EnvironmentRenderSystem::Draw")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::station::BrushPreview>()
      .each([world, brush_query, remote_player_query, drawing](
                const RaylibCamera& raylib_camera, const WindowSize& size, RenderStats& stats) {
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

        DrawScene(world, brush_query, remote_player_query, drawing->poses, raylib_camera.camera.position, stats);

        EndScene3D();
      });
}

}  // namespace

void EnvironmentRenderSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::raylib
