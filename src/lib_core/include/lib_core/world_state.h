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

#include <flecs.h>

#include <lib_core/component_meta.h>
#include <lib_core/flecs_utils.h>

namespace z13::flecs_tools {

// Tag on an entity that is part of the world state (snapshot).
struct StateEntity {};

// Trait on a component type whose values are world state; other components on a
// state entity are derived runtime data.
struct StateComponent {};

// Component properties, declared as nested types: `using State = void;` (values are
// world state; empty structs become state tags) and `using Singleton = void;` (flecs
// singleton). Nested types don't affect reflect-cpp; a base class would.
template <class T>
concept StateComponentType = requires { typename T::State; };

template <class T>
concept SingletonComponentType = requires { typename T::Singleton; };

// Registers T and applies its properties: State builds reflect-cpp meta and adds the
// StateComponent trait, Singleton the flecs Singleton trait. Only state components need
// to be reflectable aggregates; others are registered as plain runtime data. Set a
// singleton's value after registration, not in the same event.
template <class T>
flecs::untyped_component RegisterComponent(flecs::world& world) {
  const ImmediateScope immediate(world);
  flecs::untyped_component component = world.component<T>();
  if constexpr (StateComponentType<T>) {
    component = RegisterComponentMeta<T>(world);
    component.template add<StateComponent>();
  }
  if constexpr (SingletonComponentType<T>) {
    component.add(flecs::Singleton);
  }
  return component;
}

template <class... Ts>
void RegisterComponents(flecs::world& world) {
  (RegisterComponent<Ts>(world), ...);
}

// True for the component entity of a singleton that is world state (State + Singleton).
bool IsStateSingleton(flecs::entity component);

// Registers the state markers, shared meta (std::string, Eigen::Matrix4f) and the
// request queue. Called by the core before modules register.
void RegisterStateMeta(flecs::world& world);

}  // namespace z13::flecs_tools
