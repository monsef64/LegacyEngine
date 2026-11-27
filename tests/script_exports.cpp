#include "scripting/script_exports.hpp"
#include "scripting/script_components.hpp"
#include "editor/script_editor.hpp"
#include "project/project_io.hpp"
#include "editor/editor_history.hpp"
#include "editor/level_objects.hpp"
#include "audio/audio_preview.hpp"
#include <cassert>
int main(){
    const std::string source=R"(
global GameObject go
global AudioSource speaker
global AudioClip sound = "tone.wav"
global number speed = 3.5
global boolean active = true
global string title = "Hello world"
function onStart()
    assert(go:getName() == "Target")
    assert(speed == 7)
    assert(active == true)
    assert(title == "Hello world")
    speaker:setVolume(0.25)
    assert(sound == "tone.wav")
    self:setPosition(speed,0,0)
end
)";
    std::vector<ScriptField> fields;syncScriptExports(source,fields);assert(fields.size()==6);assert(fields[3].number==3.5f && fields[4].boolean);
    fields[3].number=7;syncScriptExports(source,fields);assert(fields[3].number==7);
    World world;auto owner=world.create();world.add(owner,Transform{});auto target=world.create();world.add(target,ObjectName{"Target"});world.add(target,AudioSource{});
    fields[0].entity=target;fields[1].entity=target;
    LuaScript scripts;scripts.path="Main.ls";scripts.fields=fields;world.add(owner,scripts);
    LuaConsole console;console.bindWorld(world);assert(console.bindObject(owner));assert(console.bindExports(fields));console.execute(source);
    for(auto& line:console.lines())assert(line.kind!=LuaConsole::Kind::Error);
    assert(world.get<Transform>(owner).position.x==7 && world.get<AudioSource>(target).volume==.25f);
    ProjectIO::Meshes meshes{};auto encoded=ProjectIO::encode(world,"",OrbitCamera{},meshes);ProjectIO::Loaded loaded;ProjectIO::decode(encoded,loaded,meshes);
    assert(ProjectIO::encode(loaded.world,"",OrbitCamera{},meshes)==encoded);
    auto& restored=loaded.world.get<LuaScript>(loaded.world.entities()[0]).fields;assert(restored[0].entity==loaded.world.entities()[1]);
    EditorHistory history(world);world.get<LuaScript>(owner).fields[0].entity={};history.commit(world,owner);Entity selection;assert(history.undo(world,selection));assert(world.get<LuaScript>(owner).fields[0].entity==target);
    world.get<LuaScript>(owner).fields[0].entity=owner;auto duplicate=duplicateLevelObject(world,owner);assert(world.get<LuaScript>(duplicate).fields[0].entity==duplicate);
    world.destroy(target);console.reset();assert(console.bindObject(owner));assert(console.bindExports(fields));console.execute("assert(go == nil and speaker == nil)");for(auto& line:console.lines())assert(line.kind!=LuaConsole::Kind::Error);
    syncScriptExports("global GameObject self\nglobal Unknown bad\nglobal number n = 2\nglobal number n = 3",fields);assert(fields.size()==1 && fields[0].name=="n");
    const std::string protectedSource=R"LS(
local text=[==[
global number ignored = 99
global number alsoIgnored = 2
]==]
--[=[
global number hidden = 1
global number hiddenToo = 1
]=]
local escaped="first\
global number inString = 5"
-- global number notMetadata = 1
  global number visible = 4
global boolean legacy = true
)LS";
    std::vector<ScriptField> declarations;syncScriptExports(protectedSource,declarations);
    assert(declarations.size()==2 && declarations[0].name=="visible" && declarations[1].name=="legacy");
    auto preprocessed=preprocessLegacyScript(protectedSource);assert(preprocessed.size()==protectedSource.size());
    assert(preprocessed.find("global number ignored")!=std::string::npos && preprocessed.find("global number hidden")!=std::string::npos);
    LuaConsole syntax;assert(syntax.execute(protectedSource));
    assert(!syntax.execute("global number count = 1\n\nerror('line-preserved')"));
    bool linePreserved=false;for(const auto& line:syntax.lines())if(line.kind==LuaConsole::Kind::Error && line.text.find(":3:")!=std::string::npos)linePreserved=true;assert(linePreserved);
    syntax.clear();assert(!syntax.execute("globalized wrong number 1"));
    const auto temp=std::filesystem::temp_directory_path()/("legacy-exports-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));AssetManager assets;assets.root=temp;
    assets.writeScript("Runtime.ls","global number amount = 9\nfunction onStart() self:setPosition(amount,0,0) end");
    auto runtimeOwner=world.create();world.add(runtimeOwner,Transform{});world.add(runtimeOwner,LuaScript{"Runtime.ls",true,{}});
    MeshHandle runtimeMeshes[4]{};ScriptComponents runtime;runtime.start(world,assets,runtimeMeshes);assert(world.get<Transform>(runtimeOwner).position.x==9);runtime.stop();
    ScriptEditor editor;editor.bindWorld(world,{},{},{});editor.setAssetContext("Runtime.ls",runtimeOwner);editor.setSource(assets.readScript("Runtime.ls"));world.get<Transform>(runtimeOwner).position.x=0;assert(editor.executeSource());assert(world.get<Transform>(runtimeOwner).position.x==9);
    {AudioPreview preview(true);preview.toggle(assets,"../escape.wav");assert(!preview.error.empty() && preview.path.empty());}
    std::filesystem::remove_all(temp);
}
