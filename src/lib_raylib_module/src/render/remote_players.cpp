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

#include "remote_players.h"

#include <format>
#include <string>

#include <raylib.h>
#include <raymath.h>

#include <lib_core/utils/log.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/point_text.h>
#include <z13/components/input/action_listener.h>
#include <z13/components/gameplay/player_color.h>

#include "../tools/math_convert.h"
#include "render_components.h"

namespace z13::raylib {

namespace {

::Color ToRaylibColor(const z13::gameplay::Rgb& rgb) {
  const auto channel = [](float value) { return static_cast<unsigned char>(value * kMaxColorChannel); };
  return {channel(rgb[0]), channel(rgb[1]), channel(rgb[2]), static_cast<unsigned char>(kMaxColorChannel)};
}

constexpr std::chrono::seconds kAvatarLogInterval {5};

}  // namespace

void ChaseDrawnPoses(SmoothedDrawing& drawing, const VisualSmoothing& settings, const DrawnQuery& drawn_query) {
  const auto now = std::chrono::steady_clock::now();
  const float delta_seconds =
      drawing.last_frame ? std::chrono::duration<float>(now - *drawing.last_frame).count() : 0.f;
  drawing.last_frame = now;
  const z13::math::PoseSmoothingParams params {
      .smooth_time_seconds = settings.smooth_time_seconds,
      .rotation_smooth_time_seconds = settings.rotation_smooth_time_seconds,
      .snap_distance = settings.snap_distance,
      .snap_angle_rad = z13::math::ToRadians(settings.snap_angle_deg),
  };
  z13::ChaseDrawnPoses(
      drawing.poses, drawn_query, delta_seconds, params, z13::HasInAncestry<z13::input::CurrentActionListenerTag>);
}

void DrawRemotePlayers(const flecs::world& world, const RemotePlayerQuery& remote_players, const GhostQuery& ghosts,
                       const z13::DrawnPoses& drawn, const z13::station::rooms::ViewCulling& culling,
                       float avatar_radius) {
  if (!world.has<AvatarModel>()) {
    return;
  }
  const AvatarModel& avatar = world.get<AvatarModel>();
  if (!avatar.res || avatar.res->model->meshCount == 0) {
    return;
  }
  const auto draw = [&](flecs::entity e, uint32_t player_id, const Eigen::Matrix4f& transform) {
    const Eigen::Matrix4f drawn_transform = z13::DrawnTransform(drawn, e, transform);
    const Eigen::Vector3f centre = z13::math::ExtractTranslation<float>(drawn_transform);
    const Eigen::Vector3f reach = Eigen::Vector3f::Constant(avatar_radius);
    if (!culling.Visible({centre - reach, centre + reach})) {
      return;
    }
    avatar.res->model->transform = EigenToRaylibMatrix(drawn_transform);
    DrawModel(*avatar.res->model, Vector3Zero(), 1.f, ToRaylibColor(z13::gameplay::PlayerColor(player_id)));
  };
  remote_players.each([&](flecs::entity e, const gameplay::Player& player, const Eigen::Matrix4f& transform) {
    draw(e, player.id, transform);
  });
  ghosts.each([&](flecs::entity e, const gameplay::GhostPlayer& ghost, const Eigen::Matrix4f& transform) {
    draw(e, ghost.id, transform);
  });
}

void LogRemotePlayers(const flecs::world& world, const RemotePlayerQuery& remote_players, SmoothedDrawing& drawing,
                      const SceneView& view, float avatar_radius) {
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
    const Eigen::Vector3f reach = Eigen::Vector3f::Constant(avatar_radius);
    text += std::format("; player {} (entity {}) at {}, drawn at {}, {}", player.id, e.id(),
                        PointText(z13::math::ExtractTranslation<float>(transform)), PointText(centre),
                        view.culling.Visible({centre - reach, centre + reach}) ? "visible" : "culled");
  });
  log_debug("{}", text);
}

}  // namespace z13::raylib
