# sdl_raylib_cube — isolation stand

Minimal check of the architecture in `../../plan-migracii-ogre-raylib-sdl-imgui.md`:
**SDL owns** the window / GL context / event loop / timing; **raylib** is used only
in `rlgl`-standalone mode (`rlglInit` + the content API `GenMeshCube` / `DrawMesh`,
no `InitWindow` / `BeginDrawing` / `IsKeyDown` / `GetFrameTime`); **Dear ImGui** is
an independent UI layer via the official `imgui_impl_sdl2` + `imgui_impl_opengl3`
backends (knows nothing about raylib).

Scene: one spinning cube lit by a single directional light (custom GLSL 330 via
`LoadShaderFromMemory`), plus an ImGui "Stand controls" window (fps, spin toggle
+ speed, cube color, render-module toggle, ImGui demo). `ResetGLStateToBaseline()`
runs after the raylib pass and again after the ImGui pass, before every
`SDL_GL_SwapWindow` (plan §4).

## Build

`vendor/` holds prebuilt deps (not committed):

- `vendor/raylib/`   — raylib **5.5** win64 msvc16 release
- `vendor/raylib60/` — raylib **6.0** win64 msvc16 release (optional; enables the
  `sdl_raylib_cube60` target)
- `vendor/SDL2/`     — SDL2 2.30.9 devel-VC
- `vendor/imgui/`    — Dear ImGui **v1.91.5** source (core + `backends/imgui_impl_sdl2`
  + `backends/imgui_impl_opengl3`, built into each target; self-contained GL loader)

```
cmake -S . -B build -A x64
cmake --build build --config Release
build/sdl_raylib_cube/sdl_raylib_cube.exe       # raylib 5.5
build/sdl_raylib_cube60/sdl_raylib_cube60.exe   # raylib 6.0
```

## Result (2026-09-09, dev laptop, Nahimic / A-Volute injected)

| config | present | Responding | fps |
|---|---|---|---|
| raylib **5.5**, SDL window + rlgl | lit cube on screen | true | steady ~240 |
| raylib **6.0**, SDL window + rlgl | lit cube on screen | true | steady ~240 |
| raylib **6.0**, SDL window + rlgl **+ Dear ImGui** | cube + working ImGui panel on screen | true | steady ~240 |

`AudioDevProps2.dll` is in every process and does **not** break the SDL-owned
present path. raylib's `rlgl` batch and ImGui's OpenGL3 backend coexist fine
(flush raylib, `ResetGLStateToBaseline()`, then ImGui, then swap).

Contrast: the full Zodiac13 engine on **raylib 6.0's own GLFW window path**
(`InitWindow` / `EndDrawing`) hangs black on the same machine.

Takeaway: the plan's "SDL owns the window, raylib is only an rlgl render module,
ImGui is an independent layer" architecture makes raylib **6.0** usable here
(skeletal animation back on the table), sidestepping the injector interaction
with raylib's window path. The ImGui layer is **not** a factor in the crash.
