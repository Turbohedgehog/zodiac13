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

#include <type_traits>

namespace z13::flecs_tools {

// Tag on an entity that is part of the world state (snapshot).
struct StateEntity {};

// Trait on a component type whose values are world state; other components on a
// state entity are derived runtime data.
struct StateComponent {};

// Trait on a singleton that belongs to one network session, see ResetSessionScopedComponents.
struct SessionScopedComponent {};

// Component properties, declared as nested types: `using State = void;` (values are
// world state; empty structs become state tags), `using Singleton = void;` (flecs
// singleton) and `using SessionScoped = void;` (a singleton reset per network session).
// Nested types don't affect reflect-cpp; a base class would.
// The alias must be void: an ordinary nested type that happens to be called State would
// otherwise enrol its component into every snapshot.
template <class T>
concept StateComponentType = requires { requires std::is_void_v<typename T::State>; };

template <class T>
concept SingletonComponentType = requires { requires std::is_void_v<typename T::Singleton>; };

template <class T>
concept SessionScopedComponentType = requires { requires std::is_void_v<typename T::SessionScoped>; };

}  // namespace z13::flecs_tools
