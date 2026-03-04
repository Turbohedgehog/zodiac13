#include "ogre_tools.h"

#include <filesystem>

#include <Eigen/Dense>

#include <lib_core/math.h>
#include <lib_core/log.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <ogre_module/ogre_datatypes.h>
#include <ogre_module/ogre_components.h>
#include <z13_module/components/gameplay.h>
#include <z13_module/components/geometry.h>

#include <Ogre.h>

#include <OgreRoot.h>
#include <OgreOverlaySystem.h>
#include "ogre_import/OgreImGuiOverlay.h"
#include "ogre_import/OgreImGuiInputListener.h"
#include "ogre_import/SDLInputMapping.h"
#include <OgreOverlayManager.h>
#include <OgreShaderPrerequisites.h>
#include <OgreShaderGenerator.h>
#include <OgreSGTechniqueResolverListener.h>

#include <RenderSystems/GL3Plus/OgreGL3PlusPlugin.h>
#include <Plugins/Assimp/OgreAssimpLoader.h>
#include <Plugins/PCZSceneManager/OgrePCZPlugin.h>
#include <Plugins/PCZSceneManager/OgrePCZSceneManager.h>
#include <Plugins/FreeImageCodec/OgreFreeImageCodec.h>
#include <Plugins/STBICodec/OgreSTBICodec.h>
#include "input_publisher/input_publisher.h"

#include "scene_node_holder/scene_node_holder.h"

namespace z13::ogre {

constexpr auto kPCZSceneManagerName = "PCZSceneManager"; 

Ogre::z13::ImGuiOverlay* InitImguiOverlay() {
  if(auto overlay = Ogre::OverlayManager::getSingleton().getByName("ImGuiOverlay"))
    return static_cast<Ogre::z13::ImGuiOverlay*>(overlay);

  auto* imgui_overlay = new Ogre::z13::ImGuiOverlay();
  Ogre::OverlayManager::getSingleton().addOverlay(imgui_overlay);

  float vp_scale = Ogre::OverlayManager::getSingleton().getPixelRatio();
  ImGui::GetStyle().ScaleAllSizes(vp_scale);

  return imgui_overlay;
}

void InitImgui() {
  Ogre::z13::ImGuiOverlay* overlay = InitImguiOverlay();
  float vp_scale = Ogre::OverlayManager::getSingleton().getPixelRatio();
  ImGui::GetIO().FontGlobalScale = std::round(vp_scale);

  overlay->setZOrder(300);
  overlay->show();
}

void CreateLight(Ogre::SceneNode* parent_node) {
  auto* scene_manager = parent_node->getCreator();
  scene_manager->setAmbientLight(Ogre::ColourValue::White);
  // scene_manager->setAmbientLight(Ogre::ColourValue::Green);

  auto* light_node = parent_node->createChildSceneNode("DirectionalLight");
  auto* light = scene_manager->createLight("DirectionalLight");
  light->setType(Ogre::Light::LT_SPOTLIGHT);
  light->setSpotlightRange(Ogre::Degree(30.f), Ogre::Degree(45.f), 2.f);

  light->setDiffuseColour(1.0, 1.0, 1.0);
  light->setSpecularColour(1.0, 1.0, 1.0);

  light_node->setFixedYawAxis(true, Ogre::Vector3::UNIT_Z);
  light_node->setDirection(Ogre::Vector3::UNIT_X);

  light_node->attachObject(light);
}

void LoadRooms(OgreData& ogre_data) {
  auto* scene_manager = static_cast<Ogre::PCZSceneManager*>(ogre_data.ogre_root->getSceneManager(kPCZSceneManagerName));
  auto* root_node = scene_manager->getRootSceneNode();
  auto* room_scene_node = root_node->createChildSceneNode();
  auto* room_entity = scene_manager->createEntity("Spaceship", "models/rooms/rooms.fbx");
  room_scene_node->attachObject(room_entity);

  room_scene_node->setFixedYawAxis(true, Ogre::Vector3::UNIT_Z);
  room_scene_node->setDirection(Ogre::Vector3::UNIT_X);
}

Ogre::SceneNode* LoadDemoMesh(OgreData& ogre_data) {
  // const auto& scene_managers = ogre_data.ogre_root->getSceneManagers();
  // for (const auto& [n, sm] : scene_managers) {
  //   LOG_INFO("SM name = {}", n);
  // }
  auto* scene_manager = static_cast<Ogre::PCZSceneManager*>(ogre_data.ogre_root->getSceneManager(kPCZSceneManagerName));
  // scene_manager->addPCZSceneNode()
  auto* root_node = scene_manager->getRootSceneNode();
  auto* spaceship_scene_node = root_node->createChildSceneNode();
  // auto* spaceship_entity = scene_manager->createEntity("Spaceship", "models/spaceship/spaceship.fbx");
  auto* spaceship_entity = scene_manager->createEntity("Spaceship", "models/spaceship2/spaceship.fbx");
  spaceship_scene_node->attachObject(spaceship_entity);
  spaceship_scene_node->setPosition(30.f, 0.f, 0.f);
  spaceship_scene_node->setScale(0.1f, 0.1f, 0.1f);
  auto rotation =
      Eigen::Quaternionf::Identity()
      * Eigen::AngleAxisf(z13::math::ToRadians(90.), Eigen::Vector3f::UnitX())
      * Eigen::AngleAxisf(z13::math::ToRadians(-90.), Eigen::Vector3f::UnitY());
  spaceship_scene_node->setOrientation(rotation.w(), rotation.x(), rotation.y(), rotation.z());

  return spaceship_scene_node;
}

void OgreTools::CreateSdlOgreRoot(flecs::world world, OgreData& ogre_data) {
  // SDL_SCANCODE_SPACE
  SDL_Init(SDL_INIT_VIDEO);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  auto* sld_window = SDL_CreateWindow(
      "Zodiac 13",
      SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED,
      // 1280, 720,
      640, 480,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
  );

  SDL_GLContext glContext = SDL_GL_CreateContext(sld_window);
  SDL_GL_MakeCurrent(sld_window, glContext);

  SDL_SysWMinfo wm_info;
  SDL_VERSION(&wm_info.version);
  if (!SDL_GetWindowWMInfo(sld_window, &wm_info)) {
    LOG_CRITICAL("Could not get window info: {}", SDL_GetError());
    SDL_DestroyWindow(sld_window);
    SDL_Quit();
    return;
  }

  auto ogre_root = std::make_shared<Ogre::Root>("plugins.cfg", "", "Ogre.log");
  ogre_root->installPlugin(OGRE_NEW Ogre::GL3PlusPlugin);
  ogre_root->installPlugin(OGRE_NEW Ogre::AssimpPlugin);
  // ogre_root->installPlugin(OGRE_NEW Ogre::PCZPlugin);
  Ogre::STBIImageCodec::startup();

  auto& rgm = Ogre::ResourceGroupManager::getSingleton();
  auto media_path = std::filesystem::path(OGRE_MEDIA_DIR);
  rgm.addResourceLocation((media_path / "Main").string(), "FileSystem", Ogre::RGN_INTERNAL);
  rgm.addResourceLocation((media_path / "RTShaderLib").string(), "FileSystem", Ogre::RGN_INTERNAL);

  if (!ogre_root->restoreConfig() && !ogre_root->showConfigDialog(nullptr)) {
    LOG_CRITICAL("Ogre configuration canceled!");
    SDL_DestroyWindow(sld_window);
    SDL_Quit();
    return;
  }

  Ogre::NameValuePairList misc_params;
#if defined(_WIN32)
  misc_params["externalWindowHandle"] = Ogre::StringConverter::toString((size_t)wm_info.info.win.window);
#elif defined(__linux__)
  misc_params["parentWindowHandle"] = Ogre::StringConverter::toString((size_t)wm_info.info.x11.window);
#endif

  auto& render_systems = ogre_root->getAvailableRenderers();
  if (render_systems.empty()) {
    LOG_CRITICAL("NO RENDER SYSTEMS AVALIABLE");
  }
  Ogre::RenderSystem* renderSystem = ogre_root->getAvailableRenderers()[0];
  ogre_root->setRenderSystem(renderSystem);

  ogre_root->initialise(false);
        
  Ogre::RenderWindow* ogre_window = ogre_root->createRenderWindow(
      "OGRE RenderWindow",
      // 1280, 720,
      640, 480,
      false,
      &misc_params
  );

  auto* scene_manager = static_cast<Ogre::PCZSceneManager*>(ogre_root->createSceneManager(kPCZSceneManagerName, kPCZSceneManagerName));
  scene_manager->init("ZoneType_Default");
  // scene_manager->setAmbientLight(Ogre::ColourValue::White);
  // scene_manager->setAmbientLight(Ogre::ColourValue(0.5, 0.5, 0.5));
  // auto* scene_manager = ogre_root->createSceneManager("DefaultSceneManager");
  auto* mesh_manager = ogre_root->getMeshManager();
  if (Ogre::RTShader::ShaderGenerator::initialize()) {
    auto* shader_generator = Ogre::RTShader::ShaderGenerator::getSingletonPtr();

    // Create and register the material manager listener if it doesn't exist yet.
    auto* material_mgr_listener = new OgreBites::SGTechniqueResolverListener(shader_generator);
    Ogre::MaterialManager::getSingleton().addListener(material_mgr_listener);
    shader_generator->addSceneManager(scene_manager);
  }

  auto* overlay_system = OGRE_NEW Ogre::OverlaySystem();
  scene_manager->addRenderQueueListener(overlay_system);

  auto assets_path = std::filesystem::path(ASSETS_DIR);
  rgm.addResourceLocation((assets_path).string(), "FileSystem", kAssetsResourceGroup, true);
  rgm.initialiseAllResourceGroups();
  Ogre::MaterialManager& matMgr = Ogre::MaterialManager::getSingleton();
  auto mat = matMgr.load("materials/skybox/skybox1.material", kAssetsResourceGroup);
  auto sky_mat = matMgr.getByName("skybox/skybox");
  if (sky_mat->getNumTechniques() > 0 ) {
    if (sky_mat->getTechnique(0)->getNumPasses() > 0) {
    }
  }

  scene_manager->setSkyBox(true, "skybox/skybox"); // ,5000, true, Ogre::Quaternion::IDENTITY, kAssetsResourceGroup);
  scene_manager->setSkyZone(0);
  InitImgui();
  
  SDL_ShowWindow(sld_window);

  ogre_data.ogre_root = ogre_root;
  ogre_data.ogre_window = ogre_window;
  ogre_data.sdl_window = sld_window;
  ogre_data.input_listener = std::make_shared<OgreBites::z13::ImGuiInputListener>();

  if (!world.has<gameplay::Pause>()) {
    OgreTools::EnableRelativeMouseMode(nullptr);
  }

  // LoadRooms(ogre_data);
  LoadDemoMesh(ogre_data);

  // auto* test_scene_node = scene_manager->createSceneNode("TestNode");
  // auto snh_ptr = SceneNodeHolder::CreateSceneNodeHolder(test_scene_node);
  // LOG_INFO("==== OgreTools 1 = {}, {}", !snh_ptr.expired(), snh_ptr.use_count());
  // LOG_INFO("==== OgreTools 2 = {}", snh_ptr.lock()->Get()->getName());
  // scene_manager->destroySceneNode(test_scene_node);
  // LOG_INFO("==== OgreTools 3 = {}, {}", !snh_ptr.expired(), snh_ptr.use_count());
  // // LOG_INFO("==== OgreTools 4 = {}", test_scene_node->getName());
}

void OgreTools::CreateCamera(flecs::entity e, const gameplay::Camera& camera, OgreData& ogre_data) {
  // LOG_INFO("~~~~ CreateCamera");
  auto* scene_manager = ogre_data.ogre_root->getSceneManagers().begin()->second;
  auto* ogre_camera = scene_manager->createCamera(camera.name);
  ogre_camera->setNearClipDistance(0.1);
  ogre_camera->setFarClipDistance(10000);

  auto* camera_parent_node = scene_manager->getRootSceneNode()->createChildSceneNode("MainCamera");
  auto* camera_node = camera_parent_node->createChildSceneNode();
  camera_node->attachObject(ogre_camera);
  camera_node->setPosition(0, 0, 0);
  camera_node->setFixedYawAxis(true, Ogre::Vector3::UNIT_Z);
  camera_node->setDirection(Ogre::Vector3::UNIT_X);
  auto* viewport = ogre_data.ogre_window->addViewport(ogre_camera);
  viewport->setBackgroundColour(Ogre::ColourValue(0.4f, 0.5f, 0.7f));

  OgreSceneNode ogre_scene_node {
    .scene_node = SceneNodeHolder::CreateSceneNodeHolder(camera_parent_node),
  };

  e.set(ogre_scene_node);

  CreateLight(camera_parent_node);
}

void OgreTools::ReadSdlEvents(flecs::world world, OgreData& ogre_data) {
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        world.add<OgreWindowClosed>();
        ogre_data.ogre_root->queueEndRendering();
        break;

      // case SDL_KEYDOWN:
      //   if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
      //     if (world.has<gameplay::Pause>()) {
      //       world.remove_all<gameplay::Pause>();
      //     } else {
      //       world.add<gameplay::Pause>();
      //     }
      //   }
      //   break;

      case SDL_WINDOWEVENT:
        switch (event.window.event) {
          case SDL_WINDOWEVENT_RESIZED:
            ogre_data.ogre_window->resize(event.window.data1, event.window.data2);
            break;

          case SDL_WINDOWEVENT_FOCUS_GAINED:
            world.set(gameplay::WindowFocusEvent{.has_focus = true});
            break;

          case SDL_WINDOWEVENT_FOCUS_LOST:
            world.set(gameplay::WindowFocusEvent{.has_focus = false});
            // world.add<gameplay::Pause>();
            break;
        }

        break;
    }

    auto ogre_event = Ogre::z13::convert(event);
    InputPublisher::PublishInput(world, ogre_event);
    Ogre::z13::ProcessEventToListener(ogre_event, ogre_data.input_listener.get());
  }
}

void OgreTools::EnableRelativeMouseMode(const gameplay::Pause* pause) {
  // LOG_INFO("==== OgreTools::EnableRelativeMouseMode = {}", reinterpret_cast<uint64_t>(pause));

  SDL_SetRelativeMouseMode(SDL_TRUE);
}

void OgreTools::DisableRelativeMouseMode(const OgreData& ogre_data, const gameplay::Pause&) {
  // LOG_INFO("==== OgreTools::DisableRelativeMouseMode");

  int width, height;
  SDL_GetWindowSize(ogre_data.sdl_window, &width, &height);
  SDL_WarpMouseInWindow(ogre_data.sdl_window, width / 2, height / 2);
  SDL_SetRelativeMouseMode(SDL_FALSE);
}

void OgreTools::RenderSdlOgreWindow(flecs::world world, OgreData& ogre_data) {
  try {
    if (!ogre_data.ogre_root->renderOneFrame()) {
      world.add<OgreWindowClosed>();
    }
  } catch(std::exception& ex) {
    LOG_CRITICAL("--- ex = {}", ex.what());
    throw;
  }
}

void OgreTools::DestroySdlOgreWindow(OgreData& ogre_data) {
  if (!ogre_data.ogre_root) {
    return;
  }

  SDL_DestroyWindow(ogre_data.sdl_window);
  SDL_Quit();

  ogre_data = OgreData();
}

void OgreTools::UpdateCamera(const gameplay::Camera&, const geometry::Transform& transform, const OgreSceneNode& ogre_scene_node) {
  if (ogre_scene_node.scene_node.expired()) {
    return;
  }

  const auto& position = transform.position;
  const auto& rotation = transform.rotation;

  auto* camera_scene_node = ogre_scene_node.scene_node.lock()->Get();
  camera_scene_node->setPosition(position.x, position.y, position.z);
  camera_scene_node->setOrientation(rotation.w, rotation.x, rotation.y, rotation.z);

  // auto* scene_manager = static_cast<Ogre::PCZSceneManager*>(camera_scene_node->getCreator());
  // auto* scene_manager = camera_scene_node->getCreator();
  // auto* directional_light = scene_manager->getSceneNode("DirectionalLight", false);
  // if (directional_light) {
  //   directional_light->setPosition(position.x, position.y, position.z);
  //   directional_light->setOrientation(rotation.w, rotation.x, rotation.y, rotation.z);
  // }
}

}  // namespace z13::ogre
