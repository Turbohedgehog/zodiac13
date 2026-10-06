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

#include <lib_core/state/component_meta.h>

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

// Saves and snapshots name components by path; the derived one differs between GCC and MSVC.
constexpr std::string_view kMatrix4fName = "Matrix4f";
constexpr std::string_view kVector3iName = "Vector3i";

template <class Scalar>
flecs::entity_t ScalarKind();

template <>
flecs::entity_t ScalarKind<float>() {
  return flecs::F32;
}

template <>
flecs::entity_t ScalarKind<int32_t>() {
  return flecs::I32;
}

// A fixed-size Eigen type as an opaque array of its scalars, in storage order.
template <class T, class Scalar>
void RegisterEigenArray(flecs::world& world, std::string_view name) {
  constexpr auto kCount = static_cast<int32_t>(T::SizeAtCompileTime);
  world.component<T>().set_name(name.data());
  world.component<T>()
      .template opaque<Scalar>(world.array<Scalar>(kCount).id())
      .serialize([](const flecs::serializer* s, const T* data) {
        for (int32_t i = 0; i < kCount; ++i) {
          s->value(ScalarKind<Scalar>(), data->data() + i);
        }
        return 0;
      })
      .count([](const T*) { return static_cast<size_t>(kCount); })
      .resize([](T*, size_t) {})
      .ensure_element([](T* data, size_t element) -> Scalar* { return data->data() + element; });
}

}  // namespace

void RegisterEigenMeta(flecs::world& world) {
  RegisterEigenArray<Eigen::Matrix4f, float>(world, kMatrix4fName);
  RegisterEigenArray<Eigen::Vector3i, int32_t>(world, kVector3iName);
}

}  // namespace z13::flecs_tools
