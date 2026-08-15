// Заглушка: статическая библиотека components должна иметь translation unit,
// чтобы быть компилируемой библиотекой (CMake FILE_SET CXX_MODULES не работает
// для INTERFACE-библиотек). Сам модуль z13.components объявлен в z13.components.cppm.
namespace z13::components_detail {
int kComponentsLibAnchor = 0;
}