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

#include <lib_core/component_meta.h>

#include <cstddef>
#include <string>

#include <Eigen/Dense>

namespace z13::flecs_tools {

void RegisterStdStringMeta(flecs::world& world) {
  world.component<std::string>()
      .opaque(flecs::String)
      .serialize([](const flecs::serializer* s, const std::string* data) {
        const char* str = data->c_str();
        return s->value(flecs::String, &str);
      })
      .assign_string([](std::string* data, const char* value) { *data = value; });
}

namespace {

constexpr int32_t kMatrix4fElementCount = 16;

}  // namespace

void RegisterEigenMeta(flecs::world& world) {
  world.component<Eigen::Matrix4f>()
      .opaque<float>(world.array<float>(kMatrix4fElementCount).id())
      .serialize([](const flecs::serializer* s, const Eigen::Matrix4f* data) {
        for (int32_t i = 0; i < kMatrix4fElementCount; ++i) {
          s->value(flecs::F32, data->data() + i);
        }
        return 0;
      })
      .count([](const Eigen::Matrix4f*) { return static_cast<size_t>(kMatrix4fElementCount); })
      .resize([](Eigen::Matrix4f*, size_t) {})
      .ensure_element([](Eigen::Matrix4f* data, size_t element) -> float* {
        return data->data() + element;
      });
}

}  // namespace z13::flecs_tools
