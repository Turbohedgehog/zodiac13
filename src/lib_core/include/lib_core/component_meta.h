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

#pragma once

#include <string>
#include <type_traits>

#include <flecs.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <rfl.hpp>
#include <rfl/num_fields.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace z13::flecs_tools {

// flecs has no built-in meta for std::string; register it as an opaque string.
// Call once per world before registering components that have std::string members.
void RegisterStdStringMeta(flecs::world& world);

// Registers T as a flecs component and derives its meta members from reflect-cpp
// compile-time reflection, instead of a hand-written .member() list.
//
// Requirements on T: aggregate without base classes; each member is either a
// flecs primitive or a type whose meta is already registered (std::string via
// RegisterStdStringMeta). Empty structs are registered as plain tags.
template <class T>
flecs::untyped_component RegisterComponentMeta(flecs::world& world) {
  auto component = world.component<T>();

  if constexpr ((rfl::num_fields<T>) > 0) {
    T probe{};
    rfl::to_view(probe).apply([&](const auto& field) {
      using FieldType = std::remove_pointer_t<
          typename std::remove_cvref_t<decltype(field)>::Type>;
      component.template member<FieldType>(std::string(field.name()).c_str());
    });
  }

  return component;
}

template <class... Ts>
void RegisterComponentsMeta(flecs::world& world) {
  (RegisterComponentMeta<Ts>(world), ...);
}

}  // namespace z13::flecs_tools
