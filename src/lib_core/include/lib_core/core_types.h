#pragma once

#include <memory>
#include <functional>

namespace flecs {

struct world;

}  // namespace flecs

namespace z13 {

class Core;
using CoreRef = std::reference_wrapper<Core>;

class ModuleFactoryBase;
using ModuleFactoryPtr = std::shared_ptr<ModuleFactoryBase>;

using WorldId = uint32_t;
using WorldRef = std::reference_wrapper<flecs::world>;

}  // namespace z13
