// z13.core.core_types module partition unit.
// Перенесён из include/lib_core/core_types.h.

module;

#include <cstdint>
#include <functional>
#include <memory>

export module z13.core.core_types;

namespace flecs {

struct world;
struct entity;

}  // namespace flecs

export namespace z13 {

class Core;
using CoreRef = std::reference_wrapper<Core>;

class ModuleFactoryBase;
using ModuleFactoryPtr = std::shared_ptr<ModuleFactoryBase>;

using WorldId = uint32_t;
using WorldRef = std::reference_wrapper<flecs::world>;

class ModuleLibHolder;

}  // namespace z13