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
#include <filesystem>
#include <format>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <lib_core/settings/config.h>
#include <lib_core/state/rollback.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/file_io.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/point_text.h>
#include <lib_core/utils/system_times.h>
#include <lib_core/utils/drawn_poses.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/player.h>
#include <z13/components/player_parts.h>
#include <z13/components/action_listener.h>
#include <z13/components/player_color.h>
#include <z13/components/rooms.h>
#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <z13/components/station_mode.h>
#include <primitives/chunk_mesh.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <rooms/view_culling.h>
#include <rooms/room_cache.h>
#include <z13_settings/settings.h>

#include <raylib_module/raylib_components.h>

#include "../tools/assimp_loader.h"
#include "../tools/math_convert.h"
#include "block_checker.h"
#include "block_chunks.h"
#include "block_meshes.h"
#include "cell_lighting.h"
#include "frame_profile.h"
#include "lights.h"
#include "render_components.h"
#include "render_resources.h"
#include "render_stats.h"
#include "render_tour.h"
#include "shadow_maps.h"
#include "skybox.h"
#include "station_props.h"

namespace z13::raylib {

using z13::station::rooms::CameraView;
using z13::station::rooms::ViewCulling;

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

// One directional "sun", Z-up world, shining from above (60, 40, 80) towards the origin.
const Eigen::Vector3f kSunDirection = Eigen::Vector3f(-60.f, -40.f, -80.f).normalized();
const Eigen::Vector3f kSunColor = Eigen::Vector3f::Ones();
// Bluish, as light from the sky around would be; scaled by render.outside_fill.
const Eigen::Vector3f kFillTint(0.8f, 0.87f, 1.f);
// Warm, as an LED torch; scaled by render.flashlight_intensity.
const Eigen::Vector3f kFlashlightTint(1.f, 0.95f, 0.85f);
// Keeps the ship scene's faces the sun misses from going black.
const Eigen::Vector3f kShipAmbientSky(0.30f, 0.32f, 0.40f);
const Eigen::Vector3f kShipAmbientGround(0.12f, 0.10f, 0.09f);

constexpr std::chrono::seconds kFrameLogInterval {1};

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RaylibCamera, Skybox, RenderModel, AvatarModel, Lighting, BlockMeshes,
                                         BlockChunks, ShadowMaps, CellLighting, RenderStats, RenderTour, StationProps,
                                         FrameProfile, z13::VisualSmoothing, z13::RenderTuning>(world);
}

// Everything not the local player's own chases its simulated transform in real time, so
// rollback corrections glide in.
struct SmoothedDrawing {
  z13::DrawnPoses poses;
  std::optional<std::chrono::steady_clock::time_point> last_frame;
  std::optional<std::chrono::steady_clock::time_point> last_avatar_log;
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

Lighting LoadLighting(const AssetSource& assets) {
  ::Shader shader = LoadLightingShader(assets);
  if (shader.id == 0) {
    return Lighting{};
  }

  auto res = std::make_shared<LightingResources>();
  res->shader = MakeManagedShader(shader);
  res->checker = SetupCheckerUniforms(shader);
  res->lights = FindLightUniforms(shader);
  return Lighting{.res = std::move(res)};
}

RenderModel LoadSpaceship(const Lighting& lighting, const AssetSource& assets) {
  auto loaded = LoadModelFromAsset(assets, kSpaceshipAsset);
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
void ReconcileSpaceship(flecs::world world, const Lighting& lighting, const AssetSource& assets) {
  if (z13::flecs_tools::IsCatchingUp(world)) {
    return;
  }
  const bool wanted = world.has<gameplay::Gameplay>() && !world.has<z13::station::StationMode>();
  if (wanted && !world.has<RenderModel>()) {
    world.set<RenderModel>(LoadSpaceship(lighting, assets));
  } else if (!wanted && world.has<RenderModel>()) {
    world.remove<RenderModel>();
  }
}

void StartProfiledFrame(flecs::world& world, z13::flecs_tools::FrameKind) {
  FrameProfile& profile = world.get_mut<FrameProfile>();
  const RenderStats& stats = world.get<RenderStats>();
  const auto now = FrameProfile::Clock::now();
  profile.systems.Sample(world, profile.system_totals);
  profile.timeline.StartFrame(now, world.has<gameplay::Pause>(), StagesOf(stats.times), profile.system_totals);
  if (profile.log_each_second && (!profile.last_logged || now - *profile.last_logged >= kFrameLogInterval)) {
    profile.last_logged = now;
    log_info("[frame] {}", ProfileLine(profile.timeline.Recorder(), stats));
  }
}

// Only a host knows its scene: a joining client's station comes without its props.
void ReconcileStationProps(
    flecs::world world, const Lighting& lighting, const AssetSource& assets, const z13::RenderTuning& tuning) {
  if (z13::flecs_tools::IsCatchingUp(world)) {
    return;
  }
  const auto* choice = world.try_get<z13::station::StationSceneChoice>();
  const bool wanted = lighting.res && world.has<z13::station::StationMode>() && choice && choice->scene;
  if (wanted && !world.has<StationProps>()) {
    auto props = StationProps::Load(assets, *choice->scene, *lighting.res->shader, tuning.prop_tree_loose_ratio);
    if (props) {
      log_info("[raylib] {} props in '{}'", props->Count(), *choice->scene);
      world.set<StationProps>(*std::move(props));
    } else {
      log_error("[raylib] props of '{}': {}", *choice->scene, props.error());
    }
  } else if (!wanted && world.has<StationProps>()) {
    world.remove<StationProps>();
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
using FlashlightQuery = flecs::query<const gameplay::Flashlight, const Eigen::Matrix4f>;

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
void DrawRemotePlayers(const flecs::world& world, const RemotePlayerQuery& remote_players, const z13::DrawnPoses& drawn,
                       const ViewCulling& culling) {
  if (!world.has<AvatarModel>()) {
    return;
  }
  const AvatarModel& avatar = world.get<AvatarModel>();
  if (!avatar.res || avatar.res->model->meshCount == 0) {
    return;
  }
  remote_players.each([&](flecs::entity e, const gameplay::Player& player, const Eigen::Matrix4f& transform) {
    const Eigen::Matrix4f drawn_transform = z13::DrawnTransform(drawn, e, transform);
    const Eigen::Vector3f centre = z13::math::ExtractTranslation<float>(drawn_transform);
    const Eigen::Vector3f reach = Eigen::Vector3f::Constant(kAvatarRadius);
    if (!culling.Visible({centre - reach, centre + reach})) {
      return;
    }
    avatar.res->model->transform = EigenToRaylibMatrix(drawn_transform);
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

// Brush previews, drawn one by one past the chunks.
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
      const ::Color tint = preview.valid ? valid_tint : kRefusedBuildTint;
      DrawBlock(drawing, preview.block, tint);
      for (const z13::station::Block& part : preview.parts) {
        DrawBlock(drawing, part, tint);
      }
    }
    return;
  }
  const Eigen::Vector3f point = z13::math::ExtractTranslation<float>(transform) / z13::station::kCellSize;
  DrawBlock(drawing, z13::building::primitives::PlaceCentredOn(point, z13::station::CubeSpec()), kBrushPreviewTint);
}

// A box corner's index has one bit per axis (x, y, z), set for the high side of the axis.
constexpr std::array kAxisBits {1, 2, 4};
constexpr int kBoxCorners = 1 << kAxisBits.size();

// The 12 edges of a box in world coordinates, straight into the line batch: no matrix
// stack, whose state this renderer sets by hand (BeginScene3D).
void AddBoxEdges(const Eigen::Vector3f& low, const Eigen::Vector3f& high) {
  const auto corner = [&](int index) {
    Eigen::Vector3f point = low;
    for (size_t axis = 0; axis < kAxisBits.size(); ++axis) {
      if ((index & kAxisBits[axis]) != 0) {
        point[static_cast<Eigen::Index>(axis)] = high[static_cast<Eigen::Index>(axis)];
      }
    }
    return point;
  };
  // Corners differing in exactly one bit are joined.
  for (int index = 0; index < kBoxCorners; ++index) {
    for (const int bit : kAxisBits) {
      if ((index & bit) == 0) {
        const Eigen::Vector3f from = corner(index);
        const Eigen::Vector3f to = corner(index | bit);
        rlVertex3f(from.x(), from.y(), from.z());
        rlVertex3f(to.x(), to.y(), to.z());
      }
    }
  }
}

// The debug overlay's boxes as wireframes, when it is on: on the default shader and texture
// (the block shader would light the lines by view angle) and past the depth test, as the
// edges lie on the walls' own faces.
void DrawRoomOverlay(const flecs::world& world) {
  const auto* overlay = world.try_get<z13::station::RoomOverlay>();
  if (overlay == nullptr || overlay->mode == z13::station::RoomOverlayMode::kOff) {
    return;
  }
  rlDrawRenderBatchActive();
  rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault());
  rlSetTexture(rlGetTextureIdDefault());
  rlDisableDepthTest();
  rlBegin(RL_LINES);
  for (const z13::station::OverlayBox& box : overlay->boxes) {
    rlColor4ub(box.color[0], box.color[1], box.color[2], box.color[3]);
    AddBoxEdges(box.min.cast<float>() * z13::station::kCellSize,
                (box.min + box.extent).cast<float>() * z13::station::kCellSize);
  }
  rlEnd();
  rlDrawRenderBatchActive();
  rlEnableDepthTest();
}

// The lines the brushes' previews carry (a wall's stick and the arrow along its axis), on the
// default shader and past the depth test, like the room overlay.
void DrawBrushLines(const BrushQuery& brushes) {
  rlDrawRenderBatchActive();
  rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault());
  rlSetTexture(rlGetTextureIdDefault());
  rlDisableDepthTest();
  rlBegin(RL_LINES);
  brushes.each([](flecs::entity brush, const z13::building::Brush&, const Eigen::Matrix4f&) {
    if (!brush.has<z13::station::BrushPreview>()) {
      return;
    }
    const auto& preview = brush.get<z13::station::BrushPreview>();
    const ::Color tint = preview.valid ? kValidBuildTint : kRefusedBuildTint;
    rlColor4ub(tint.r, tint.g, tint.b, 255);
    for (const z13::station::BrushLine& line : preview.lines) {
      const Eigen::Vector3f from = line.from * z13::station::kCellSize;
      const Eigen::Vector3f to = line.to * z13::station::kCellSize;
      rlVertex3f(from.x(), from.y(), from.z());
      rlVertex3f(to.x(), to.y(), to.z());
    }
  });
  rlEnd();
  rlDrawRenderBatchActive();
  rlEnableDepthTest();
}

BlockMeshes::OptionalPalette PaletteOf(const flecs::world& world) {
  const auto* palette = world.try_get<z13::building::primitives::BlockPalette>();
  return palette != nullptr ? BlockMeshes::OptionalPalette(palette->palette) : std::nullopt;
}

std::vector<z13::station::Block> CollectBlocks(const BlockQuery& query) {
  Z13_PROFILE_ZONE("CollectBlocks");
  std::vector<z13::station::Block> blocks;
  query.each([&blocks](const z13::station::Block& block) { blocks.push_back(block); });
  return blocks;
}

// The blocks of the primitives named in `replaced` leave out: props stand in for them.
std::vector<z13::station::Block> CollectShownBlocks(const BlockQuery& query, const std::vector<std::string>& replaced,
                                                    BlockMeshes::OptionalPalette palette) {
  std::vector<z13::station::Block> blocks = CollectBlocks(query);
  if (replaced.empty() || !palette) {
    return blocks;
  }
  std::vector<uint32_t> hidden;
  for (const std::string& name : replaced) {
    if (const auto primitive = palette->get().Find(name)) {
      hidden.push_back(primitive->get().id);
    }
  }
  std::erase_if(blocks, [&hidden](const z13::station::Block& block) {
    return std::ranges::contains(hidden, block.spec.type_id);
  });
  return blocks;
}

// Rebuilds the chunks only when blocks, the chunk size, the palette or the replaced primitives
// changed; `synced_replaced` is what the last rebuild left out.
void SyncBlockChunks(BlockChunks& chunks, const BlockQuery& blocks, const z13::RenderTuning& tuning,
                     BlockMeshes::OptionalPalette palette, const std::vector<std::string>& replaced,
                     std::vector<std::string>& synced_replaced) {
  // Checked first: iterating the query resets its changed state.
  if (blocks.changed() || !chunks.SyncedWith(tuning.chunk_cells, palette) || replaced != synced_replaced) {
    chunks.Sync(CollectShownBlocks(blocks, replaced, palette), palette, tuning.chunk_cells);
    synced_replaced = replaced;
  }
}

// The screen spans 2 in normalized device coordinates along each axis.
Eigen::Vector2f PixelsInNdc(float pixels, const Eigen::Vector2i& screen) {
  constexpr float kNdcSpan = 2.f;
  return (kNdcSpan * pixels) * screen.cast<float>().cwiseMax(1.f).cwiseInverse();
}

// The view-projection rlgl draws with, set by BeginScene3D.
Eigen::Matrix4f CurrentViewProjection() {
  return RaylibToEigenMatrix(rlGetMatrixProjection()) * RaylibToEigenMatrix(rlGetMatrixModelview());
}

ViewCulling::OptionalGraph CurrentRooms(const z13::station::rooms::RoomCache* cache) {
  return cache != nullptr ? cache->Current() : std::nullopt;
}

// The overlay outlines the rooms drawn this frame (station_module builds its boxes).
void PublishSeenRooms(const ViewCulling& culling, const z13::station::rooms::RoomCache* rooms, RenderStats& stats,
                      z13::station::RoomOverlay* overlay) {
  stats.rooms_seen = culling.SeenRoomCount();
  if (overlay == nullptr) {
    return;
  }
  overlay->drawn.reset();
  const auto fingerprint = rooms != nullptr ? rooms->CurrentFingerprint() : std::nullopt;
  if (overlay->mode == z13::station::RoomOverlayMode::kSeen && fingerprint) {
    if (auto seen = culling.SeenRooms()) {
      overlay->drawn = z13::station::RoomsDrawn {.fingerprint = *fingerprint, .rooms = std::move(*seen)};
    }
  }
}

void WriteTourFile(const std::filesystem::path& path, const std::string& csv) {
  if (const auto written = z13::WriteFile(path, csv); !written) {
    log_error("[raylib] render tour: {}", written.error());
  } else {
    log_info("[raylib] render tour written to '{}'", path.string());
  }
}

// Places the camera at the tour's stop for this frame. Returns false once the tour is over,
// after writing what it measured.
bool StepRenderTour(RaylibCamera& camera, RenderTour& tour, const TourFrame& frame) {
  if (!tour.Started()) {
    if (frame.rooms && tour.ReadyToStart()) {
      tour.Start(*frame.rooms, frame.chunk_cells);
      log_info("[raylib] render tour: {} stops", tour.StopCount());
    }
    return true;
  }
  const size_t before = tour.StopIndex();
  tour.Step(RenderTour::Clock::now(), frame.stats, frame.sample_systems);
  if (tour.StopIndex() != before) {
    log_info("[raylib] render tour: stop {} of {} measured", tour.StopIndex(), tour.StopCount());
  }
  if (const auto stop = tour.Current()) {
    const Eigen::Vector3f eye = stop->view.eye * z13::station::kCellSize;
    const Eigen::Vector3f target = stop->view.target * z13::station::kCellSize;
    camera.camera.position = {eye.x(), eye.y(), eye.z()};
    camera.camera.target = {target.x(), target.y(), target.z()};
    camera.camera.up = {0.f, 0.f, 1.f};
    return true;
  }
  WriteTourFile(tour.Output(), tour.Csv());
  WriteTourFile(tour.SystemsOutput(), tour.SystemsCsv());
  return false;
}

template <typename Draw>
double MsOf(Draw draw) {
  const auto start = std::chrono::steady_clock::now();
  draw();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void SetViewPosition(const flecs::world& world, const ::Vector3& eye) {
  if (!world.has<Lighting>()) {
    return;
  }
  const Lighting& lighting = world.get<Lighting>();
  if (lighting.res) {
    const std::array view_pos = {eye.x, eye.y, eye.z};
    SetShaderValue(*lighting.res->shader, lighting.res->shader->locs[SHADER_LOC_VECTOR_VIEW], view_pos.data(),
                   SHADER_UNIFORM_VEC3);
  }
}

// Every player as its avatar, the local one too: it is drawn only in shadows.
std::vector<MovingCaster> PlayerCasters(const DrawnQuery& players, const z13::DrawnPoses& drawn) {
  const Eigen::Vector3f reach = Eigen::Vector3f::Constant(kAvatarRadius);
  std::vector<MovingCaster> casters;
  players.each([&](flecs::entity e, const Eigen::Matrix4f& transform) {
    const Eigen::Matrix4f drawn_transform = z13::DrawnTransform(drawn, e, transform);
    const Eigen::Vector3f centre = z13::math::ExtractTranslation<float>(drawn_transform);
    casters.push_back({.entity = e.id(), .transform = drawn_transform, .box = {centre - reach, centre + reach}});
  });
  return casters;
}

ShadowCasters CastersOf(const BlockChunks& chunks, const AvatarModel* avatar, std::vector<MovingCaster> players) {
  ShadowCasters casters {
      .draw_static = [&chunks](const CasterFilter& filter,
                               const ::Material& material) { chunks.DrawShadowCasters(filter, material); },
      .static_version = chunks.Version(),
      .changed_since = [&chunks](uint64_t version) { return chunks.ChangedSince(version); },
      .static_bounds = chunks.OpaqueBounds(),
      .draw_moving = [](const MovingCaster&, const ::Material&) {},
  };
  if (avatar == nullptr || !avatar->res || avatar->res->model->meshCount == 0) {
    return casters;
  }
  const ::Mesh& mesh = avatar->res->model->meshes[0];
  casters.draw_moving = [&mesh](const MovingCaster& caster, const ::Material& material) {
    DrawMesh(mesh, material, EigenToRaylibMatrix(caster.transform));
  };
  casters.moving = std::move(players);
  return casters;
}

// The flashlights switched on, from each player's eye along its look, nearest first.
std::vector<FrameLight> FlashlightsOf(const FlashlightQuery& flashlights, const z13::DrawnPoses& drawn,
                                      const ViewCulling& culling, const Eigen::Vector3f& eye,
                                      const z13::RenderTuning& tuning) {
  const float range = tuning.flashlight_range_m;
  const float cos_half_angle = std::cos(z13::math::ToRadians(tuning.flashlight_half_angle_deg));
  std::vector<FrameLight> lights;
  flashlights.each([&](flecs::entity e, const gameplay::Flashlight& flashlight, const Eigen::Matrix4f& transform) {
    if (!flashlight.on) {
      return;
    }
    const Eigen::Matrix4f drawn_transform = z13::DrawnTransform(drawn, e, transform);
    const Eigen::Vector3f position = z13::math::ExtractTranslation<float>(drawn_transform);
    const Eigen::AlignedBox3f radius_box(position - Eigen::Vector3f::Constant(range),
                                         position + Eigen::Vector3f::Constant(range));
    if (!culling.Visible(radius_box)) {
      return;
    }
    lights.push_back({
        .position = position,
        .radius = range,
        .color = kFlashlightTint * tuning.flashlight_intensity,
        .reach = radius_box,
        .radius_box = radius_box,
        .cone = SpotCone {.direction = drawn_transform.block<3, 1>(0, 0).normalized(), .cos_half_angle = cos_half_angle},
        .owner = e.id(),
    });
  });
  std::ranges::sort(lights, {}, [&eye](const FrameLight& light) { return (light.position - eye).squaredNorm(); });
  return lights;
}

struct LitScene {
  // The flashlights, then the lamps players cast shadows from.
  std::vector<FrameLight> lights;
  SceneLight scene;
  ShadowAtlasLayout atlas;
};

// The lamps whose radius holds a player, nearest a player first, at most `count`. Per
// component: this runs over every lamp each frame.
std::vector<FrameLight> PlayerShadowLamps(const CellLighting& lighting, std::span<const MovingCaster> players,
                                          size_t count) {
  std::vector<Eigen::Vector3f> centres;
  std::ranges::transform(players, std::back_inserter(centres),
                         [](const MovingCaster& player) { return player.box.center(); });
  const float cell = z13::station::kCellSize;
  std::vector<std::pair<float, size_t>> near;
  const std::vector<z13::station::light::Lamp>& lamps = lighting.ShiningLamps();
  for (size_t i = 0; i < lamps.size(); ++i) {
    const Eigen::Vector3f& position = lamps[i].position;
    const float radius = lamps[i].radius * cell;
    float nearest = radius * radius;
    for (const Eigen::Vector3f& centre : centres) {
      const float dx = (position.x() * cell) - centre.x();
      const float dy = (position.y() * cell) - centre.y();
      const float dz = (position.z() * cell) - centre.z();
      nearest = std::min(nearest, (dx * dx) + (dy * dy) + (dz * dz));
    }
    if (nearest < radius * radius) {
      near.emplace_back(nearest, i);
    }
  }
  std::ranges::sort(near);
  std::vector<FrameLight> chosen;
  for (size_t i = 0; i < std::min(count, near.size()); ++i) {
    const z13::station::light::Lamp& lamp = lamps[near[i].second];
    const Eigen::Vector3f position = lamp.position * cell;
    const Eigen::Vector3f reach = Eigen::Vector3f::Constant(lamp.radius * cell);
    const Eigen::AlignedBox3f radius_box(position - reach, position + reach);
    chosen.push_back({.position = position,
                      .radius = lamp.radius * cell,
                      .color = lamp.color,
                      .reach = radius_box,
                      .radius_box = radius_box,
                      .use = LightUse::kPlayerShadows});
  }
  return chosen;
}

// A station is lit by its lamps' baked light, and by the sun and the fill only where the hull
// lets them in. Flashlights come first, so they get their shadows before any lamp.
LitScene LightScene(const flecs::world& world, std::vector<FrameLight> flashlights, const CellLighting* cell_lighting,
                    ShadowMaps* shadows, const ShadowCasters& casters, const z13::RenderTuning& tuning) {
  LitScene lit {.lights = std::move(flashlights)};
  lit.lights.resize(std::min(lit.lights.size(), static_cast<size_t>(tuning.frame_lights)));
  if (!world.has<z13::station::StationMode>()) {
    lit.scene.sun = DirectionalLight {.direction = kSunDirection, .color = kSunColor};
    lit.scene.ambient_sky = kShipAmbientSky;
    lit.scene.ambient_ground = kShipAmbientGround;
    return lit;
  }
  const auto shadow_lights = static_cast<size_t>(tuning.shadow_lights);
  if (cell_lighting != nullptr && !casters.moving.empty() && lit.lights.size() < shadow_lights) {
    std::ranges::move(PlayerShadowLamps(*cell_lighting, casters.moving, shadow_lights - lit.lights.size()),
                      std::back_inserter(lit.lights));
  }
  // Without the hull's shadows the sun and the fill would light the rooms too.
  if (shadows == nullptr) {
    return lit;
  }
  if (const Status lamps = shadows->UpdateLamps(lit.lights, casters, tuning); !lamps) {
    log_error("[raylib] shadows: {}", lamps.error());
  }
  lit.atlas = shadows->AtlasLayout();
  if (const auto sun_shadow = shadows->UpdateDirectional(DirectionalMap::kSun, kSunDirection, casters, tuning)) {
    lit.scene.sun =
        DirectionalLight {.direction = kSunDirection, .color = kSunColor, .shadow_view_projection = sun_shadow};
  }
  if (const auto fill_shadow = shadows->UpdateDirectional(DirectionalMap::kFill, -kSunDirection, casters, tuning)) {
    lit.scene.fill = DirectionalLight {
        .direction = -kSunDirection, .color = kFillTint * tuning.outside_fill, .shadow_view_projection = fill_shadow};
  }
  return lit;
}

// As BeginScene3D set it up.
ClusterView CurrentClusterView(const Eigen::Vector2i& size) {
  const Eigen::Matrix4f view = RaylibToEigenMatrix(rlGetMatrixModelview());
  const ::Matrix projection = rlGetMatrixProjection();
  return {
      .view = view,
      .focal = {projection.m0, projection.m5},
      // The view looks down its -Z.
      .forward = -view.block<1, 3>(2, 0).transpose(),
      .screen = size,
      .near = static_cast<float>(rlGetCullDistanceNear()),
      .far = static_cast<float>(rlGetCullDistanceFar()),
  };
}

void UploadLighting(const flecs::world& world, const LitScene& lit, const ShadowMaps* shadows,
                    const ClusterView& view, const z13::RenderTuning& tuning) {
  if (!world.has<Lighting>()) {
    return;
  }
  const Lighting& lighting = world.get<Lighting>();
  if (!lighting.res) {
    return;
  }
  std::vector<FrameLight> lit_lights;
  std::ranges::copy_if(lit.lights, std::back_inserter(lit_lights),
                       [](const FrameLight& light) { return light.use == LightUse::kLights; });
  lighting.res->clusters.Update(lit_lights, view, tuning);
  lighting.res->clusters.Upload(*lighting.res->shader, lighting.res->lights);
  lighting.res->clusters.Bind();
  UploadShadowAtlas(*lighting.res->shader, lighting.res->lights, lit.atlas);
  UploadSceneLight(*lighting.res->shader, lighting.res->lights, lit.scene);
  UploadPlayerShadows(*lighting.res->shader, lighting.res->lights, lit.lights);
  if (shadows != nullptr) {
    shadows->Bind();
  }
  const auto* cell_lighting = world.try_get<CellLighting>();
  const int cell_light_off = 0;
  if (cell_lighting != nullptr && world.has<z13::station::StationMode>()) {
    cell_lighting->Upload(*lighting.res->shader, lighting.res->lights);
    cell_lighting->Bind();
  } else {
    SetShaderValue(*lighting.res->shader, lighting.res->lights.cell_light_enabled, &cell_light_off,
                   SHADER_UNIFORM_INT);
  }
}

// The skybox and the spaceship model, drawn before the scene.
void DrawBackground(const flecs::world& world) {
  if (world.has<Skybox>()) {
    if (const Skybox& skybox = world.get<Skybox>(); skybox.res) {
      DrawSkybox(*skybox.res);
    }
  }
  if (world.has<RenderModel>()) {
    if (const RenderModel& render_model = world.get<RenderModel>(); render_model.res) {
      DrawModel(*render_model.res->model, Vector3Zero(), 1.f, WHITE);
    }
  }
}

struct SceneView {
  ::Vector3 eye {};
  ViewCulling culling;
};

constexpr std::chrono::seconds kAvatarLogInterval {5};

// Debug: what DrawRemotePlayers decided for each remote avatar, every few seconds.
void LogRemotePlayers(const flecs::world& world, const RemotePlayerQuery& remote_players, SmoothedDrawing& drawing,
                      const SceneView& view) {
  const auto now = std::chrono::steady_clock::now();
  if (drawing.last_avatar_log && now - *drawing.last_avatar_log < kAvatarLogInterval) {
    return;
  }
  drawing.last_avatar_log = now;
  using z13::math::PointText;
  const AvatarModel* avatar = world.try_get<AvatarModel>();
  std::string text = std::format(
      "[raylib] avatars: eye {}, seen rooms {}, avatar model {}", PointText({view.eye.x, view.eye.y, view.eye.z}),
      view.culling.SeenRoomCount() ? std::to_string(*view.culling.SeenRoomCount()) : "unculled",
      avatar && avatar->res ? std::to_string(avatar->res->model->meshCount) + " meshes" : "missing");
  remote_players.each([&](flecs::entity e, const gameplay::Player& player, const Eigen::Matrix4f& transform) {
    const Eigen::Vector3f centre =
        z13::math::ExtractTranslation<float>(z13::DrawnTransform(drawing.poses, e, transform));
    const Eigen::Vector3f reach = Eigen::Vector3f::Constant(kAvatarRadius);
    text += std::format("; player {} (entity {}) at {}, drawn at {}, {}", player.id, e.id(),
                        PointText(z13::math::ExtractTranslation<float>(transform)), PointText(centre),
                        view.culling.Visible({centre - reach, centre + reach}) ? "visible" : "culled");
  });
  log_debug("{}", text);
}

// Players and opaque chunks first; then glass from the farthest and previews, blended over
// them and without writing depth, so nothing behind them is hidden.
void DrawScene(const flecs::world& world, const BrushQuery& brushes, const RemotePlayerQuery& remote_players,
               const z13::DrawnPoses& drawn, const SceneView& view, RenderStats& stats) {
  stats = {};
  stats.times.players_ms = MsOf([&] { DrawRemotePlayers(world, remote_players, drawn, view.culling); });
  if (!world.has<BlockMeshes>() || !world.has<BlockChunks>() || !world.has<Lighting>()) {
    return;
  }
  const Lighting& lighting = world.get<Lighting>();
  const SingleBlockDrawing drawing {
      .meshes = world.get_mut<BlockMeshes>(), .lighting = lighting, .palette = PaletteOf(world)};
  const BlockChunks& chunks = world.get<BlockChunks>();
  stats.times.opaque_ms = MsOf([&] {
    chunks.DrawOpaque(view.culling, lighting, stats);
    if (const auto* props = world.try_get<StationProps>()) {
      // The blocks leave the checker on, which replaces every texture.
      StopChecker(lighting);
      props->Draw(view.culling);
    }
    rlDrawRenderBatchActive();
  });

  rlDisableDepthMask();
  stats.times.glass_ms = MsOf([&] {
    chunks.DrawTransparent(view.culling, Eigen::Vector3f(view.eye.x, view.eye.y, view.eye.z), lighting, stats);
  });
  stats.times.previews_ms = MsOf([&] {
    brushes.each([&world, &drawing](flecs::entity brush, const z13::building::Brush&, const Eigen::Matrix4f& transform) {
      DrawBrushPreview(world, brush, transform, drawing);
    });
    rlDrawRenderBatchActive();
  });
  rlEnableDepthMask();
  stats.times.overlay_ms = MsOf([&] {
    StopChecker(lighting);
    DrawRoomOverlay(world);
    DrawBrushLines(brushes);
  });
  stats.times.release_ms = MsOf([&] { drawing.meshes.get().ReleaseUnused(); });
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
  world.set<FrameProfile>({});
  // The frame report names the heaviest systems.
  ecs_measure_system_time(world, true);
  z13::flecs_tools::OnFrameStart(world, StartProfiledFrame);
  // Shared by the closures below; lives as long as the world.
  auto drawing = std::make_shared<SmoothedDrawing>();

  // RaylibData is set right after InitWindow, so a live GL context is guaranteed.
  world.observer<RaylibData, const z13::Assets>("EnvironmentRenderSystem::LoadEnvironment")
      .event(flecs::OnAdd)
      // Only RaylibData's arrival loads.
      .term_at(1).filter()
      .yield_existing()
      .each([](flecs::iter& it, size_t, RaylibData&, const z13::Assets& assets) {
        flecs::world world = it.world();
        Lighting lighting = LoadLighting(assets.source);
        world.set<AvatarModel>(LoadAvatar(lighting));
        const ::Shader shader = lighting.res ? *lighting.res->shader : ::Shader {};
        world.set<BlockMeshes>(BlockMeshes(shader));
        world.set<BlockChunks>(BlockChunks(shader, assets.source));
        const ::Shader shadow_shader = LoadShadowShader(assets.source);
        if (shadow_shader.id == 0) {
          log_error("[raylib] the shadow shader didn't load: nothing casts shadows, the sun is off in a station");
        }
        world.set<ShadowMaps>(ShadowMaps(shadow_shader));
        world.set<CellLighting>({});
        world.set<Lighting>(std::move(lighting));
        world.set<Skybox>(LoadSkybox(assets.source));
        const auto config = z13::GetCoreConfig(world);
        world.get_mut<FrameProfile>().log_each_second = config && config->get().IsFrameLog();
        if (const auto tour = config ? config->get().GetRenderTourPath() : std::nullopt) {
          world.set(RenderTour(*tour, config->get().GetRenderTourViews()));
          ecs_measure_system_time(world, true);
        }
      });

  // Everything below runs in the Render phase, ahead of Draw (systems in one phase run
  // in registration order), so the scene is synced with this frame's final state.
  world.system<const Lighting, const z13::Assets>("EnvironmentRenderSystem::ReconcileSpaceship")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<gameplay::Gameplay>()
      .read<z13::station::StationMode>()
      .write<RenderModel>()
      .each([](flecs::iter& it, size_t, const Lighting& lighting, const z13::Assets& assets) {
        ReconcileSpaceship(it.world(), lighting, assets.source);
      });

  world.system<const Lighting, const z13::Assets, const z13::RenderTuning>(
           "EnvironmentRenderSystem::ReconcileStationProps")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::station::StationMode>()
      .read<z13::station::StationSceneChoice>()
      .write<StationProps>()
      .each([](flecs::iter& it, size_t, const Lighting& lighting, const z13::Assets& assets,
               const z13::RenderTuning& tuning) {
        ReconcileStationProps(it.world(), lighting, assets.source, tuning);
      });

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

  const FlashlightQuery flashlight_query =
      world.query_builder<const gameplay::Flashlight, const Eigen::Matrix4f>("EnvironmentRenderSystem::FlashlightQuery")
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
      .read<StationProps>()
      .each([block_query, synced_replaced = std::make_shared<std::vector<std::string>>()](
                flecs::iter& it, size_t, BlockChunks& chunks, const z13::RenderTuning& tuning,
                const z13::building::primitives::BlockPalette* palette) {
        const auto* props = it.world().try_get<StationProps>();
        SyncBlockChunks(chunks, block_query, tuning,
                        palette != nullptr ? BlockMeshes::OptionalPalette(palette->palette) : std::nullopt,
                        props != nullptr ? props->Replaced() : std::vector<std::string> {}, *synced_replaced);
      });

  // Its own query: SyncBlockChunks resets the other's changed state.
  const BlockQuery light_block_query =
      world.query_builder<const z13::station::Block>("EnvironmentRenderSystem::LightBlockQuery")
          .detect_changes()
          .build();
  world.system<CellLighting, const z13::building::primitives::BlockPalette*>("EnvironmentRenderSystem::SyncCellLighting")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::station::Block>()
      .each([light_block_query](CellLighting& lighting, const z13::building::primitives::BlockPalette* palette) {
        const BlockMeshes::OptionalPalette shown =
            palette != nullptr ? BlockMeshes::OptionalPalette(palette->palette) : std::nullopt;
        if (light_block_query.changed() || !lighting.SyncedWith(shown)) {
          if (const Status synced = lighting.Sync(CollectBlocks(light_block_query), shown); !synced) {
            log_error("[raylib] lamps: {}", synced.error());
          }
        }
      });

  // After SyncRaylibCamera, so the tour's camera wins.
  world.system<RaylibCamera, RenderTour, const RenderStats, const z13::station::rooms::RoomCache*,
               const z13::RenderTuning>("EnvironmentRenderSystem::StepRenderTour")
      .kind<Render>()
      .tick_source<RenderGate>()
      .with<z13::input::CurrentActionListenerTag>()
      .write<RaylibWindowClosed>()
      .each([](flecs::iter& it, size_t, RaylibCamera& camera, RenderTour& tour, const RenderStats& stats,
               const z13::station::rooms::RoomCache* rooms, const z13::RenderTuning& tuning) {
        flecs::world world = it.world();
        const TourFrame frame {
            .stats = stats,
            .rooms = CurrentRooms(rooms),
            .chunk_cells = tuning.chunk_cells,
            .sample_systems = [world] { return z13::SystemTimes(world); },
        };
        if (!world.has<RaylibWindowClosed>() && !StepRenderTour(camera, tour, frame)) {
          world.add<RaylibWindowClosed>();
        }
      });

  // Render phase: 3D scene between FrameBegin (PreRender) and FrameEnd (FinalizeRender).
  world.system<const RaylibCamera, const WindowSize, RenderStats, FrameProfile, const z13::station::rooms::RoomCache*,
               z13::station::RoomOverlay*, const RenderTour*, const z13::RenderTuning, const CellLighting*,
               ShadowMaps*>("EnvironmentRenderSystem::Draw")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::station::BrushPreview>()
      .each([brush_query, remote_player_query, drawn_query, flashlight_query, drawing](
                flecs::iter& it, size_t, const RaylibCamera& raylib_camera, const WindowSize& size, RenderStats& stats,
                FrameProfile& profile, const z13::station::rooms::RoomCache* rooms, z13::station::RoomOverlay* overlay,
                const RenderTour* tour, const z13::RenderTuning& tuning, const CellLighting* cell_lighting,
                ShadowMaps* shadows) {
        flecs::world world = it.world();
        profile.timeline.MarkDrawStart(FrameProfile::Clock::now());
        double background_ms = MsOf([&] { SetViewPosition(world, raylib_camera.camera.position); });
        BeginScene3D(raylib_camera.camera, size.size);

        const ::Vector3& eye = raylib_camera.camera.position;
        const auto culling_start = std::chrono::steady_clock::now();
        const CameraView camera {
            .view_projection = CurrentViewProjection(),
            .eye = Eigen::Vector3f(eye.x, eye.y, eye.z),
            .min_portal_extent = PixelsInNdc(tuning.min_portal_pixels, size.size),
        };
        const SceneView view {
            .eye = eye,
            .culling = ViewCulling(camera, tour == nullptr || tour->CullsRooms() ? CurrentRooms(rooms) : std::nullopt),
        };
        const double culling_us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - culling_start).count();

        LitScene lit;
        const double shadows_ms = MsOf([&] {
          if (!world.has<BlockChunks>()) {
            return;
          }
          const ShadowCasters casters = CastersOf(world.get<BlockChunks>(), world.try_get<AvatarModel>(),
                                                  PlayerCasters(drawn_query, drawing->poses));
          lit = LightScene(world, FlashlightsOf(flashlight_query, drawing->poses, view.culling, camera.eye, tuning),
                           cell_lighting, shadows, casters, tuning);
          // The shadow passes drew elsewhere, with their own viewport and matrices.
          rlViewport(0, 0, size.size.x(), size.size.y());
          BeginScene3D(raylib_camera.camera, size.size);
        });
        background_ms += MsOf([&] {
          UploadLighting(world, lit, shadows, CurrentClusterView(size.size), tuning);
          DrawBackground(world);
        });

        DrawScene(world, brush_query, remote_player_query, drawing->poses, view, stats);
        LogRemotePlayers(world, remote_player_query, *drawing, view);
        stats.culling_us = culling_us;
        stats.times.background_ms = background_ms;
        stats.times.shadows_ms = shadows_ms;
        stats.lights = lit.lights.size();
        stats.debug_view = world.has<Lighting>() && world.get<Lighting>().res ? world.get<Lighting>().res->debug_view : 0;
        stats.shadowed_lights =
            std::ranges::count_if(lit.lights, [](const FrameLight& light) { return light.shadow_slot.has_value(); });
        const ::Camera3D& pose = raylib_camera.camera;
        stats.camera = z13::PoseLookingAt(Eigen::Vector3f(pose.position.x, pose.position.y, pose.position.z),
                                          Eigen::Vector3f(pose.target.x, pose.target.y, pose.target.z));
        PublishSeenRooms(view.culling, rooms, stats, overlay);

        stats.times.flush_ms = MsOf(EndScene3D);
      });
}

}  // namespace

void EnvironmentRenderSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::raylib
