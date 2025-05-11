#pragma once

#include <memory>

namespace flecs {

struct world;

}  // namespace flecs

namespace z13 {

class Core;

class ModuleBase;
using ModulePtr = std::shared_ptr<ModuleBase>;

class SystemBase;
using SystemPtr = std::shared_ptr<SystemBase>;
using SystemWeakPtr = std::weak_ptr<SystemBase>;

class World;
using WorldId = uint32_t;
using WorldPtr = std::shared_ptr<World>;
using WorldWeakPtr = std::weak_ptr<World>;

using ECS = flecs::world;

}  // namespace z13
