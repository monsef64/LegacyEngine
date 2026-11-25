// Standalone game player: no editor windows, asset browser, project saving or tools.
#include "animation/animation_system.hpp"
#include "animation/bone_attachment_system.hpp"
#include "assets/texture_preview.hpp"
#include "audio/audio_system.hpp"
#include "editor/input_settings.hpp"
#include "effects/laser_update.hpp"
#include "gameplay/first_person.hpp"
#include "gameplay/third_person.hpp"
#include "levels/level_system.hpp"
#include "levels/preload_screen.hpp"
#include "navigation/navigation.hpp"
#include "particles/particle_update.hpp"
#include "physics/physics.hpp"
#include "prefabs/prefab.hpp"
#include "project/game_export.hpp"
#include "project/layers.hpp"
#include "project/rendering_settings.hpp"
#include "rendering/renderer.hpp"
#include "rendering/scene_surface.hpp"
#include "rendering/shader_data.hpp"
#include "scene/scene_camera.hpp"
#include "scripting/game_services.hpp"
#include "scripting/script_components.hpp"
#include "ui/game_ui.hpp"
#include "ui/script_gui.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <chrono>
#include <imgui.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace {
std::filesystem::path executablePath() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!count || count >= path.size()) {
        throw std::runtime_error("Cannot locate game executable");
    }
    path.resize(count);
    return path;
#else
    std::array<char, 4096> path{};
    const auto count = readlink("/proc/self/exe", path.data(), path.size());
    if (count <= 0 || std::size_t(count) >= path.size()) {
        throw std::runtime_error("Cannot locate game executable");
    }
    return std::string(path.data(), std::size_t(count));
#endif
}
void play(SDL_Window* window, Renderer& renderer, const GameExport::Mounted& package) {
    World world;
    const ProjectIO::Meshes meshes{renderer.createMesh(makeGrid(32, 1)),
                                   renderer.createMesh(makePlane(16, 16)),
                                   renderer.createMesh(makeBox({1, 1, 1})),
                                   renderer.createMesh(makeSphere(.5f)),
                                   renderer.createMesh(makeCapsule(.5f, 1)),
                                   renderer.createMesh(makeCone(.5f, 1))};
    const MeshHandle scriptMeshes[]{meshes[2], meshes[3], meshes[4], meshes[5]};
    std::string source, error, assetDirectory;
    OrbitCamera editorCamera;
    if (!ProjectIO::load((package.root / "Game.legacy").string(),
                         world,
                         source,
                         editorCamera,
                         meshes,
                         error,
                         &assetDirectory)) {
        throw std::runtime_error(error);
    }
    AssetManager assets;
    assets.root = package.root / "Assets";
    Prefabs::validateLinks(assets, world, meshes);
    updateHierarchy(world);
    LayerSettings layers;
    layers.load(assets);
    RenderingSettings rendering;
    rendering.load(assets);
    renderer.setShadowSettings(rendering.shadows);
    renderer.setAssetRoot(assets.root);
    TextureQuality::lifecycle(true);
    LevelSystem levels(meshes);
    LevelSystem::active = &levels;
    levels.setRoot(assets.root);
    levels.commit(package.metadata.value("startLevel", std::string{}), false);
    LuaConsole global;
    ScriptComponents scripts;
    Physics physics;
    physics.setAssetRoot(assets.root);
    AnimationSystem animations;
    Navigation::System navigation;
    AudioSystem audio;
    InputState input;
    input.window = window;
    InputSettings bindings;
    bindings.load(assets, input);
    GameUI ui;
    ScriptGUI gui;
    SceneSurface surface;
    ui.clicked = [](Entity owner, const std::string& id) {
        LuaConsole::postEvent(GameUI::clickEvent(owner, id));
    };
    auto bindScripts = [&] {
        global.reset();
        global.setAssetRoot(assets.root);
        global.bindWorld(world, meshes[2], meshes[3], meshes[4], meshes[5], meshes[1]);
        scripts.start(world, assets, scriptMeshes, meshes[1]);
        if (!source.empty() && !global.execute(source, "Game")) {
            throw std::runtime_error("Startup script failed; inspect the player log");
        }
        audio.start(world);
        Particles::reset(world);
        updateHierarchy(world);
        std::uint64_t key = 14695981039346656037ull;
        for (unsigned char c : levels.current()) {
            key ^= c;
            key *= 1099511628211ull;
        }
        const auto nav =
            package.root / "Settings" / ("Navigation_" + std::to_string(key) + ".json");
        navigation.clear();
        if (std::filesystem::is_regular_file(nav)) {
            navigation.load(nav);
        }
    };
    GameServices::configure(window,
                            false,
                            package.metadata.value("company", std::string{}),
                            package.metadata.at("name").get<std::string>(),
                            package.root);
    GameServices::update(window, true);
    auto preloaded = preloadBeforePlay(
        assets, meshes, ProjectIO::encode(world, source, OrbitCamera{}, meshes), renderer, window);
    bindScripts();
    audio.publishPreloaded(std::move(preloaded.audio), assets.root);
    animations.publishPreloaded(std::move(preloaded.clips), assets.root);
    LuaConsole::postEvent("onLevelLoaded", {levels.current()});
    // Window icon on Linux uses the exported PNG. Windows shell/executable icons
    // and version resources are stamped during native export.
#ifndef _WIN32
    try {
        AssetManager icons;
        icons.root = package.root;
        const auto image =
            loadTextureThumbnail(icons, package.metadata.at("icon").get<std::string>());
        auto* icon = SDL_CreateSurfaceFrom(128,
                                           128,
                                           SDL_PIXELFORMAT_RGBA32,
                                           const_cast<unsigned char*>(image.pixels.data()),
                                           128 * 4);
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    } catch (...) {
    }
#endif
    auto last = SDL_GetTicksNS();
    bool running = true, suspended = false;
    Entity mouseOver{}, pressedObject{};
    auto notify =
        [&](Entity entity, const char* event, Entity other = Entity{}, bool collision = false) {
            if (entity != Entity{} && isActive(world, entity)) {
                global.objectEvent(entity, event, other, collision);
                scripts.objectEvent(entity, event, other, collision);
            }
        };
    while (running) {
        const auto frameStarted = SDL_GetTicksNS();
        GameServices::update(window, true);
        if (GameServices::quitReady()) {
            break;
        }
        const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        if (!focused && !GameServices::pendingQuit()) {
            LuaConsole::dispatchEvents();
            input.releaseMouse();
            input.clear();
            audio.suspend(true);
            suspended = true;
            SDL_Event wake;
            if (SDL_WaitEvent(&wake)) {
                if (wake.type == SDL_EVENT_QUIT || wake.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    GameServices::requestQuit();
                } else {
                    SDL_PushEvent(&wake);
                }
            }
            // Drain non-close events so a background input event cannot spin forever.
            while (SDL_PollEvent(&wake)) {
                if (wake.type == SDL_EVENT_QUIT || wake.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    GameServices::requestQuit();
                }
            }
            last = SDL_GetTicksNS();
            continue;
        }
        if (suspended) {
            audio.suspend(false);
            suspended = false;
            last = SDL_GetTicksNS();
        }
        const auto now = SDL_GetTicksNS();
        double waitDelta = double(now - last) / 1e9;
        float dt = std::clamp(float(waitDelta), 0.f, .25f);
        last = now;
        int width = 1, height = 1, logicalW = 1, logicalH = 1;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        SDL_GetWindowSize(window, &logicalW, &logicalH);
        if (width <= 0 || height <= 0) {
            if (GameServices::pendingQuit()) {
                scripts.update(dt, waitDelta);
                LuaConsole::dispatchEvents();
            }
            SDL_Delay(20);
            continue;
        }
        surface.resize(width, height);
        ui.sync(world, true, assets.root, window);
        ui.viewport(0, 0, float(logicalW), float(logicalH), width, height);
        gui.viewport(0, 0, float(logicalW), float(logicalH), width, height);
        input.enabled = !ui.capturesKeyboard() && !gui.capturesKeyboard();
        input.mouseEnabled = !ui.capturesMouse() && !gui.capturesMouse();
        input.beginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                GameServices::requestQuit();
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11 &&
                !event.key.repeat) {
                SDL_SetWindowFullscreen(window,
                                        !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
            }
            const bool consumed = gui.event(event, window, true, input.mouseLocked) |
                                  ui.event(event, window, true, input.mouseLocked);
            if (!consumed) {
                input.event(event);
            }
        }
        input.synchronizeKeyboard();
        levels.preload = [&](LevelSystem::Transition& prepared) {
            renderer.setAssetRoot(assets.root);
            return renderer.preloadAssets(prepared.assets);
        };
        if (auto transition = levels.poll()) {
            scripts.stop();
            global.shutdown();
            audio.clear();
            input.releaseMouse();
            LuaConsole::clearEvents();
            gui.shutdown();
            ui.sync(world, false, assets.root, window);
            world.restore(transition->level->world);
            source = transition->level->source;
            physics.reset();
            animations.clear();
            levels.commit(transition->name, false);
            bindScripts();
            // The replacement scripts did not exist during this loading frame.
            // Do not charge their waits for either the old frame or loading time.
            dt = 0;
            waitDelta = 0;
            last = SDL_GetTicksNS();
            audio.publishPreloaded(std::move(transition->audio), assets.root);
            animations.publishPreloaded(std::move(transition->clips), assets.root);
            LuaConsole::postEvent("onLevelLoaded", {transition->name});
            mouseOver = pressedObject = {};
        }
        if (auto failure = levels.takeFailure()) {
            LuaConsole::postEvent("onLevelFailedToLoad", {failure->name, failure->error});
        }
        world.each<CameraEffects>([&](Entity, CameraEffects& fx) {
            const float step = fx.fadeSpeed * dt;
            if (fx.fadeAmount < fx.fadeTarget) {
                fx.fadeAmount = std::min(fx.fadeTarget, fx.fadeAmount + step);
            } else {
                fx.fadeAmount = std::max(fx.fadeTarget, fx.fadeAmount - step);
            }
        });
        Prefabs::updateDestroyTimers(world, dt);
        global.update(dt, waitDelta);
        scripts.update(dt, waitDelta);
        animations.update(world, assets, dt);
        const auto events = animations.events;
        for (const auto& call : events) {
            if (!call.script.empty()) {
                scripts.callNamed(call.owner, call.script, call.name);
            } else {
                notify(call.owner, call.name.c_str());
            }
        }
        LuaConsole::dispatchEvents();
        renderer.updateSkyboxFades(world, dt);
        updateHierarchy(world);
        navigation.update(world, dt);
            for (const auto& event : navigation.events) notify(event.first,event.second.c_str());
        updateHierarchy(world);
        Terrains::update(world, assets);
        physics.update(world, dt, [&](float fixed) {
            input.beginFixedInput();
            global.fixedUpdate(fixed);
            scripts.fixedUpdate(fixed);
            input.endFixedInput();
            updateCharacterControllers(world, physics);
            updateHierarchy(world);
            BoneAttachments::update(world, assets);
            updateHierarchy(world);
        });
        const auto triggers = physics.triggerEvents, collisions = physics.collisionEvents;
        const char* triggerNames[]{"onTriggerEnter", "onTriggerStay", "onTriggerExit"};
        const char* collisionNames[]{"onCollisionEnter", "onCollisionStay", "onCollisionExit"};
        for (const auto& hit : triggers) {
            notify(hit.receiver, triggerNames[hit.phase], hit.other, true);
        }
        for (const auto& hit : collisions) {
            notify(hit.receiver, collisionNames[hit.phase], hit.other, true);
        }
        const auto active = activeCamera(world);
        if (const auto* tps = world.tryGet<TPSCamera>(active);
            tps && tps->enabled && tps->lockMouse && input.enabled && !input.mouseLocked) {
            input.lockCursor(true);
        }
        updateFPSCameras(world, dt, input);
        updateTPSCameras(world, physics, dt, input);
        updateRecoil(world, dt);
        BoneAttachments::update(world, assets);
        updateHierarchy(world);
        updateCameras(world, dt);
        audio.update(world, assets, true);
        std::vector<std::pair<Entity, int>> audioEvents;
        world.each<AudioSource>([&](Entity e, AudioSource& source) {
            for (int event : source.pendingEvents) {
                audioEvents.emplace_back(e, event);
            }
            source.pendingEvents.clear();
        });
        for (const auto& event : audioEvents) {
            notify(event.first, AudioSource::eventNames[event.second]);
        }
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        Entity hovered{};
        const auto mouseCamera = activeCamera(world);
        if (mouseCamera != Entity{} && input.mouseEnabled && world.has<Transform>(mouseCamera)) {
            const auto& c = world.get<Camera>(mouseCamera);
            const auto& pose = world.get<Transform>(mouseCamera);
            const float u = input.mouseLocked ? .5f : mx / std::max(1, logicalW),
                        v = input.mouseLocked ? .5f : my / std::max(1, logicalH);
            if (u >= c.viewportX && u <= c.viewportX + c.viewportWidth && v >= c.viewportY &&
                v <= c.viewportY + c.viewportHeight) {
                const auto nearPoint = cameraScreenToWorld(pose, c, {u, v, 0}),
                           farPoint = cameraScreenToWorld(pose, c, {u, v, 1});
                RayHit hit;
                if (physics.raycast({nearPoint, normalize(farPoint - nearPoint)}, c.farClip, hit)) {
                    hovered = hit.entity;
                }
            }
        }
        if (hovered != mouseOver) {
            notify(mouseOver, "onMouseExit");
            notify(hovered, "onMouseEnter");
            mouseOver = hovered;
        }
        notify(hovered, "onMouseOver");
        if (input.mousePressed[1]) {
            pressedObject = hovered;
            notify(hovered, "onMouseDown");
        }
        if (input.mouseReleased[1]) {
            notify(pressedObject, "onMouseUp");
            pressedObject = {};
        }
        Lasers::update(world, physics);
        Particles::update(world,
                          dt,
                          true,
                          false,
                          {},
                          1,
                          [&](Entity emitter, const Ray& ray, float distance, RayHit& hit) {
                              return physics.raycast(ray, distance, hit, [&](Entity other) {
                                  return other != emitter && isActive(world, other);
                              });
                          });
        glBindFramebuffer(GL_FRAMEBUFFER, surface.framebuffer);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, width, height);
        glClearColor(.1f, .1f, .1f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const auto camera = activeCamera(world);
        if (camera != Entity{} && world.has<Transform>(camera)) {
            auto& c = world.get<Camera>(camera);
            c.aspect = float(width) / height;
            const auto pose = recoilCameraPose(world, camera, world.get<Transform>(camera));
            renderer.draw(world,
                          cameraMatrix(pose, c),
                          std::max(1, int(width * c.viewportWidth)),
                          std::max(1, int(height * c.viewportHeight)),
                          cameraEye(pose, c),
                          int(c.viewportX * width),
                          int((1 - c.viewportY - c.viewportHeight) * height),
                          false,
                          world.tryGet<CameraEffects>(camera),
                          (std::uint64_t(camera.generation) << 32) | camera.index);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, surface.framebuffer);
        ui.render(width, height, true);
        gui.render(window, width, height, true, assets.root);
        GameServices::finishFrame(surface.framebuffer, width, height);
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, surface.framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(
            0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        SDL_GL_SwapWindow(window);
        GameServices::paceFrame(frameStarted);
    }
    levels.suspended = true;
    scripts.stop();
    global.shutdown();
    LuaConsole::clearEvents();
    input.releaseMouse();
    gui.shutdown();
    ui.sync(world, false, assets.root, window);
    audio.clear();
    TextureQuality::lifecycle(false);
}
} // namespace
int main(int, char**) {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    int result = 1;
    const auto original = std::filesystem::current_path();
    GameExport::Mounted package;
    try {
        GameExport::mount(executablePath(), package);
        std::filesystem::current_path(package.root / "Engine");
        engineDataRoot = package.root / "Engine" / "data";
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        window = SDL_CreateWindow(package.metadata.at("name").get<std::string>().c_str(),
                                  1280,
                                  720,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                      SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window || (context = SDL_GL_CreateContext(window)) == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_GL_SetSwapInterval(1);
        ImGui::CreateContext();
        {
            Renderer renderer;
            if (!renderer.init()) {
                throw std::runtime_error("OpenGL renderer initialization failed");
            }
            try {
                play(window, renderer, package);
            } catch (...) {
                renderer.shutdown();
                throw;
            }
            renderer.shutdown();
        }
        result = 0;
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game player: %s", error.what());
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Game failed to start", error.what(), window);
    }
    if (ImGui::GetCurrentContext()) {
        ImGui::DestroyContext();
    }
    if (context) {
        SDL_GL_DestroyContext(context);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    std::error_code ec;
    std::filesystem::current_path(original, ec);
    return result;
}
