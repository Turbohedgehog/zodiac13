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

#include <string>

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

}  // namespace z13::flecs_tools
