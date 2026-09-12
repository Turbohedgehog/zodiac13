// SDL2 owns the window / GL context / event loop / timing.
// raylib is used in rlgl-standalone mode (no InitWindow / BeginDrawing): just
// rlglInit + the content API (GenMeshCube / DrawMesh) for one lit, spinning cube.
// Dear ImGui runs as an independent UI layer via its official SDL2 + OpenGL3
// backends -- it knows nothing about raylib.
//
// Mirrors plan-migracii-ogre-raylib-sdl-imgui.md sections 1-4, as a stand to
// observe behaviour (present, vsync, overlay injectors, raylib + ImGui coexist).

#include <SDL.h>

#define USE_LIBTYPE_SHARED
#include "raylib.h"
#include "raymath.h"  // header-only (static inline), no CORE
#include "rlgl.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <cstdio>
#include <cstdlib>

static const char *kVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "out vec3 fragPosition;\n"
    "out vec3 fragNormal;\n"
    "void main() {\n"
    "    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));\n"
    "    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 1.0)));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *kFragmentShader =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec3 fragNormal;\n"
    "uniform vec3 lightDir;\n"
    "uniform vec3 lightColor;\n"
    "uniform vec3 viewPos;\n"
    "uniform vec4 ambient;\n"
    "uniform vec4 colDiffuse;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 n = normalize(fragNormal);\n"
    "    vec3 l = normalize(-lightDir);\n"
    "    float diff = max(dot(n, l), 0.0);\n"
    "    vec3 v = normalize(viewPos - fragPosition);\n"
    "    vec3 h = normalize(l + v);\n"
    "    float spec = pow(max(dot(n, h), 0.0), 32.0) * step(0.0001, diff);\n"
    "    vec3 base = colDiffuse.rgb;\n"
    "    vec3 color = base * (ambient.rgb + lightColor * diff) + lightColor * spec * 0.35;\n"
    "    finalColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);\n"
    "}\n";

// plan section 4: return to a plain state before handing off to the next layer /
// SwapBuffers. Minimal stand has no process-wide GL loader, so this uses rlgl's
// wrappers instead of raw gl* calls (equivalent effect for the state we touch).
static void ResetGLStateToBaseline(int w, int h) {
  rlDisableFramebuffer();
  rlDisableVertexArray();
  rlDisableShader();
  rlDisableScissorTest();
  rlDisableWireMode();
  rlSetBlendMode(RL_BLEND_ALPHA);
  rlViewport(0, 0, w, h);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  // Pin the GL context attributes (plan section 2): don't rely on SDL defaults.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  int width = 960;
  int height = 600;
  SDL_Window *window = SDL_CreateWindow(
      "SDL + raylib (rlgl) + ImGui - spinning cube",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) {
    std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_GL_MakeCurrent(window, gl);
  SDL_GL_SetSwapInterval(1);  // vsync

  // Render module: raylib rlgl. Its own GL loader (plan section 2.4).
  rlLoadExtensions((void *)SDL_GL_GetProcAddress);
  rlglInit(width, height);

  {
    typedef const unsigned char *(*PfnGetString)(unsigned int);
    PfnGetString gl_get_string = (PfnGetString)SDL_GL_GetProcAddress("glGetString");
    if (gl_get_string) {
      std::printf("GL_VENDOR   : %s\n", gl_get_string(0x1F00));
      std::printf("GL_RENDERER : %s\n", gl_get_string(0x1F01));
      std::printf("GL_VERSION  : %s\n", gl_get_string(0x1F02));
    }
    std::printf("rlgl version enum: %d\n", rlGetVersion());
    std::fflush(stdout);
  }

  // UI layer: Dear ImGui (own backends, independent of raylib).
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForOpenGL(window, gl);
  ImGui_ImplOpenGL3_Init("#version 330");
  std::printf("ImGui %s + impl_sdl2 + impl_opengl3 initialised\n", IMGUI_VERSION);
  std::fflush(stdout);

  Mesh cube = GenMeshCube(2.0f, 2.0f, 2.0f);
  Shader lit = LoadShaderFromMemory(kVertexShader, kFragmentShader);
  Material material = LoadMaterialDefault();
  material.shader = lit;
  material.maps[MATERIAL_MAP_DIFFUSE].color = Color{220, 150, 90, 255};

  const int loc_light_dir = rlGetLocationUniform(lit.id, "lightDir");
  const int loc_light_color = rlGetLocationUniform(lit.id, "lightColor");
  const int loc_view_pos = rlGetLocationUniform(lit.id, "viewPos");
  const int loc_ambient = rlGetLocationUniform(lit.id, "ambient");

  const Vector3 light_dir = {-0.6f, -1.0f, -0.4f};
  const Vector3 light_color = {1.0f, 0.96f, 0.9f};
  const Vector4 ambient = {0.12f, 0.12f, 0.16f, 1.0f};
  rlEnableShader(lit.id);
  rlSetUniform(loc_light_dir, &light_dir, SHADER_UNIFORM_VEC3, 1);
  rlSetUniform(loc_light_color, &light_color, SHADER_UNIFORM_VEC3, 1);
  rlSetUniform(loc_ambient, &ambient, SHADER_UNIFORM_VEC4, 1);
  rlDisableShader();

  const Vector3 camera_pos = {4.0f, 3.5f, 6.0f};

  Uint64 prev = SDL_GetPerformanceCounter();
  const double freq = (double)SDL_GetPerformanceFrequency();
  float angle = 0.0f;
  int frame = 0;
  double fps_accum = 0.0;
  int fps_frames = 0;
  float last_fps = 0.0f;

  // UI-controlled state
  bool spin = true;
  float spin_speed = 0.9f;
  float cube_color[3] = {220.0f / 255.0f, 150.0f / 255.0f, 90.0f / 255.0f};
  bool show_demo = false;
  bool render_module_enabled = true;

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE &&
                 !ImGui::GetIO().WantCaptureKeyboard) {
        running = false;
      } else if (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        width = event.window.data1;
        height = event.window.data2;
      }
    }

    const Uint64 now = SDL_GetPerformanceCounter();
    const double dt = (double)(now - prev) / freq;
    prev = now;
    if (spin) angle += (float)dt * spin_speed;

    fps_accum += dt;
    ++fps_frames;
    if (fps_accum >= 1.0) {
      last_fps = (float)(fps_frames / fps_accum);
      std::printf("frame %d  ~%.0f fps  dt=%.4f\n", frame, last_fps, dt);
      std::fflush(stdout);
      fps_accum = 0.0;
      fps_frames = 0;
    }

    // ---- render module (raylib rlgl) ----
    rlViewport(0, 0, width, height);
    rlClearColor(28, 28, 38, 255);
    rlClearScreenBuffers();

    if (render_module_enabled) {
      rlEnableDepthTest();
      const Matrix projection =
          MatrixPerspective(60.0 * DEG2RAD, (double)width / (double)height, 0.1, 100.0);
      const Matrix view =
          MatrixLookAt(camera_pos, Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f});
      rlSetMatrixProjection(projection);
      rlSetMatrixModelview(view);

      rlEnableShader(lit.id);
      rlSetUniform(loc_view_pos, &camera_pos, SHADER_UNIFORM_VEC3, 1);
      rlDisableShader();

      material.maps[MATERIAL_MAP_DIFFUSE].color = Color{
          (unsigned char)(cube_color[0] * 255.0f),
          (unsigned char)(cube_color[1] * 255.0f),
          (unsigned char)(cube_color[2] * 255.0f), 255};

      const Matrix spin_m =
          MatrixMultiply(MatrixRotateX(angle * 0.6f), MatrixRotateY(angle));
      DrawMesh(cube, material, spin_m);
      rlDrawRenderBatchActive();
    }

    ResetGLStateToBaseline(width, height);

    // ---- UI layer (ImGui) ----
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Stand controls")) {
      const float imgui_fps = ImGui::GetIO().Framerate;
      ImGui::Text("%.0f FPS   %.2f ms  (stdout avg %.0f)", imgui_fps,
                  imgui_fps > 0 ? 1000.0f / imgui_fps : 0.0f, last_fps);
      ImGui::Separator();
      ImGui::Checkbox("Render module enabled", &render_module_enabled);
      ImGui::Checkbox("Spin", &spin);
      ImGui::SliderFloat("Spin speed", &spin_speed, 0.0f, 4.0f, "%.2f");
      ImGui::ColorEdit3("Cube color", cube_color);
      ImGui::Separator();
      ImGui::Checkbox("Show ImGui demo", &show_demo);
      if (ImGui::Button("Quit")) running = false;
    }
    ImGui::End();

    if (show_demo) ImGui::ShowDemoWindow(&show_demo);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ResetGLStateToBaseline(width, height);  // plan section 4 A/B: also after UI
    SDL_GL_SwapWindow(window);
    ++frame;

    if (frame == 90) {
      unsigned char *pixels = rlReadScreenPixels(width, height);
      const int idx = ((height / 2) * width + (width / 2)) * 4;
      std::printf("center pixel @ frame 90 (RGBA): %d %d %d %d\n", pixels[idx], pixels[idx + 1],
                  pixels[idx + 2], pixels[idx + 3]);
      std::fflush(stdout);
      std::free(pixels);
    }
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  UnloadShader(lit);
  UnloadMesh(cube);
  rlglClose();
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
