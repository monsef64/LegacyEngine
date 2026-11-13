#include "editor/editor_input.hpp"
#include "project/project_document.hpp"
#include "editor/play_mode.hpp"
#include <cassert>

int main() {
    assert(!cameraModifiersHeld(SDL_KMOD_NONE));
    assert(!cameraModifiersHeld(SDL_KMOD_CAPS));
    for(auto mod:{SDL_KMOD_LCTRL,SDL_KMOD_RCTRL,
        SDL_KMOD_LALT,SDL_KMOD_RALT,SDL_KMOD_LGUI,SDL_KMOD_RGUI})assert(cameraModifiersHeld(mod));
    assert(cameraModifiersHeld(SDL_KMOD_CTRL|SDL_KMOD_SHIFT));
    assert(!cameraModifiersHeld(SDL_KMOD_SHIFT));
#ifndef _WIN32
    assert(projectWindowTitle("",true)=="Untitled* - Untitled - Linux");
    assert(projectWindowTitle("/tmp/Scene.legacy",false)=="Untitled - Scene - Linux");
    assert(projectWindowTitle("/tmp/Scene.legacy",true)=="Untitled* - Scene - Linux");
#endif
    World world;Entity e=world.create();world.add(e,Transform{});OrbitCamera camera;
    auto saved=projectContentState(world,"print('saved')",camera);
    EditorHistory history(world);
    world.get<Transform>(e).position.x=2;history.commit(world,{});
    assert(projectContentState(world,"print('saved')",camera)!=saved);
    Entity selection;assert(history.undo(world,selection));
    assert(projectContentState(world,"print('saved')",camera)==saved);
    assert(projectContentState(world,"print('modified')",camera)!=saved);
    camera.yaw+=1;assert(projectContentState(world,"print('saved')",camera)!=saved);
    camera.yaw-=1;
    // The restored edit document has the same saved-state marker after Stop.
    auto before=projectContentState(world,"",camera);
    PlayMode mode;mode.play(world);world.get<Transform>(e).position.x=50;mode.stop(world);
    assert(projectContentState(world,"",camera)==before);
}
