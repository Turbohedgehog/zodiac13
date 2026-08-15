// z13.core primary module interface unit.
// Первый пилотный именованный модуль проекта (Этап 1 плана миграции на C++20 modules).
//
// Монолитный вариант заменён набором подмодулей (по одному на заголовочный файл):
//   z13.core.core_types  <- include/lib_core/core_types.h
//   z13.core.config      <- include/lib_core/config.h
//   z13.core.math        <- include/lib_core/math.h
//   z13.core.components  <- include/lib_core/components.h
//   z13.core.flecs_utils <- include/lib_core/flecs_utils.h
//   z13.core.core        <- include/lib_core/core.h
//
// Основной модуль реэкспортирует все подмодули, поэтому прежний публичный
// интерфейс `import z13.core;` сохраняется полностью.
//
// НЕ переносятся в модуль (остаются в классических заголовках, см. план):
//   - log.h      (макросы LOG_* — не экспортируются из модуля)
//   - module_factory_base.h  (extern "C" + BOOST_DLL_ALIAS — ABI-точка Boost.DLL)

export module z13.core;

export import z13.core.config;
export import z13.core.math;
export import z13.core.components;
export import z13.core.flecs_utils;
export import z13.core.core;