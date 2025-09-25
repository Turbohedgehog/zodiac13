#include "gui_render_system.h"

#include <flecs.h>

#include <lib_core/components.h>
#include <z13_module/components/z13.h>
#include <z13_module/components/gameplay.h>
#include <ogre_module/ogre_components.h>

#include <lib_core/log.h>

#include "../ogre_tools/ogre_import/OgreImGuiOverlay.h"

namespace z13::ogre {

// struct ReadEvents { };
// struct PreRender { };
// struct Render { };
struct PreRenderGui { };
struct RenderGui { };
struct PostRenderGui { };
// struct FinalizeRender { };

void BeginGui() {
  Ogre::z13::ImGuiOverlay::NewFrame();
  // LOG_INFO("~~~~ BeginGui");
}

void DrawGameplayMenu(flecs::entity e, OgreData& ogre_data) {
  if (!e.world().has<gameplay::Pause>()) {
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | 
                        ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoSavedSettings;

  if (ImGui::Begin("Modal Dialog", nullptr, flags)) {
      ImGui::Text("Main menu");
      
      // Добавляем кнопку OK для закрытия диалога
      if (ImGui::Button("Back", ImVec2(120, 0)))
      {
        e.world().remove_all<gameplay::Pause>();
      }

      if (ImGui::Button("Exit", ImVec2(120, 0)))
      {
        e.world().add<OgreWindowClosed>();
      }

      ImGui::End();
  }

  // ImGui::ShowDemoWindow();
}

void EndGui() {
  ImGui::EndFrame();
}

void RendegGuiSystem::Register(flecs::world& world) {
  world.component<PreRenderGui>().add(flecs::Phase).depends_on<Render>();
  world.component<RenderGui>().add(flecs::Phase).depends_on<PreRenderGui>();
  world.component<PostRenderGui>().add(flecs::Phase).depends_on<RenderGui>();
  world.component<FinalizeRender>().add(flecs::Phase).depends_on<PostRenderGui>();

  world.system()
    .kind<PreRenderGui>()
    .each(BeginGui);

  world.system<OgreData>()
    .kind<RenderGui>()
    .each(DrawGameplayMenu);

  world.system()
    .kind<PostRenderGui>()
    .each(EndGui);
}

}  // namespace z13::ogre
