#include "editor_history.hpp"
#include "play_mode.hpp"
#include "level_objects.hpp"
#include "playback_controls.hpp"
#include <cassert>

int main() {
    World world;
    Entity object=world.create(),selection=object;
    world.add(object,Transform{{1,2,3}});
    world.add(object,ObjectName{"Before"});
    EditorHistory history(world,selection);
    assert(!history.canUndo() && !history.canRedo());
    world.get<Transform>(object).position.x=5;
    history.observe(world,selection,true);
    world.get<Transform>(object).position.x=9;
    history.observe(world,selection,true);
    assert(!history.canUndo());
    history.observe(world,selection,false);
    assert(history.undo(world,selection));
    assert(world.get<Transform>(object).position.x==1);
    assert(!history.canUndo());
    assert(history.redo(world,selection));
    assert(world.get<Transform>(object).position.x==9);
    Entity child=world.create();world.add(child,Transform{});
    world.add(child,Collider{});world.add(child,RigidBody{});
    assert(setParent(world,child,object));
    selection=child;history.commit(world,selection);
    world.destroy(object);world.remove<Collider>(child);
    history.commit(world,selection);
    assert(history.undo(world,selection));
    assert(world.alive(object) && world.has<Collider>(child));
    assert(getParent(world,child)==object);
    assert(history.undo(world,selection));
    assert(!world.alive(child) && selection==object);
    assert(history.redo(world,selection));
    assert(world.alive(child) && selection==child);
    // Editing after undo invalidates the forward branch.
    world.get<ObjectName>(object).value="After";
    history.commit(world,selection);assert(!history.canRedo());
    // Stop restores the edit snapshot without generating a new undo entry.
    PlayMode mode;mode.play(world);
    world.get<Transform>(object).position.x=50;
    world.destroy(child);mode.stop(world);
    history.observe(world,selection,false);
    assert(history.undo(world,selection));
    assert(world.get<ObjectName>(object).value=="Before");
    // Empty entities (no Transform) must also be tracked.
    Entity empty=world.create();history.commit(world,selection);
    assert(history.undo(world,selection));assert(!world.alive(empty));
    assert(history.redo(world,selection));assert(world.alive(empty));
    // No-op frames do not create history entries.
    EditorHistory noops(world);
    for(int i=0;i<10;++i)noops.observe(world,{},false);
    assert(!noops.canUndo());
    for(int i=0;i<70;++i) {
        world.get<Transform>(object).position.y=static_cast<float>(i+10);
        noops.commit(world,{});
    }
    int count=0;while(noops.undo(world,selection))++count;
    assert(count==64);
    // Clipboard is a value snapshot and survives source deletion and ID reuse.
    World copiedWorld;
    Entity source=copiedWorld.create(),parent=copiedWorld.create();
    copiedWorld.add(source,Transform{{2,3,4}});
    copiedWorld.add(parent,Transform{});
    copiedWorld.add(source,ObjectName{"Copied"});
    copiedWorld.add(source,RigidBody{});copiedWorld.add(source,Collider{});
    assert(setParent(copiedWorld,source,parent));
    ObjectClipboard clipboard;assert(!clipboard.available());
    clipboard.copy(copiedWorld,source);assert(clipboard.available());
    Entity duplicate=duplicateLevelObject(copiedWorld,source);
    assert(getParent(copiedWorld,duplicate)==parent);
    copiedWorld.get<Transform>(source).position.x=100;
    copiedWorld.destroy(source);
    Entity pasted=clipboard.paste(copiedWorld);
    assert(copiedWorld.get<Transform>(pasted).position.x==2);
    assert(copiedWorld.get<ObjectName>(pasted).value=="Copied (Copy)");
    assert(getParent(copiedWorld,pasted)==Entity{});
    assert(copiedWorld.has<RigidBody>(pasted) && copiedWorld.has<Collider>(pasted));
    copiedWorld.get<Collider>(pasted).radius=9;
    Entity again=clipboard.paste(copiedWorld);
    assert(copiedWorld.get<Collider>(again).radius==0.5f);
    // Icon controls generate geometry without requiring Unicode font glyphs.
    ImGui::CreateContext();auto& io=ImGui::GetIO();
    io.DisplaySize=ImVec2(800,600);io.DeltaTime=1.0f/60;
    io.Fonts->Build();ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(400,120));ImGui::Begin("Controls");
    playbackButton("##Play","Play",PlaybackIcon::Play);
    playbackButton("##Pause","Pause",PlaybackIcon::Pause);
    playbackButton("##Stop","Stop",PlaybackIcon::Stop);
    ImGui::End();ImGui::Render();
    assert(ImGui::GetDrawData()->TotalVtxCount>0);
    ImGui::DestroyContext();
}
