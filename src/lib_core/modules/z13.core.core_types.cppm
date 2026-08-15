// z13.core.core_types module partition unit.
// Перенесён из include/lib_core/core_types.h.
//
// Здесь определены базовые типы/алиасы ядра.
//
// ВАЖНО: полное определение ModuleFactoryBase (ABI-интерфейс Boost.DLL)
// остаётся в include/lib_core/module_factory_base.h (см. план миграции).
// В модуле объявлен только forward-declaration, а ModuleFactoryPtr ссылается
// на него. Потребители, которым нужен доступ к членам ModuleFactoryBase,
// должны включить <lib_core/module_factory_base.h>.

module;

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

export module z13.core.core_types;

export namespace z13 {

class Core;
using CoreRef = std::reference_wrapper<Core>;

class ModuleFactoryBase;
using ModuleFactoryPtr = std::shared_ptr<ModuleFactoryBase>;

using WorldId = uint32_t;

class ModuleLibHolder;

}  // namespace z13