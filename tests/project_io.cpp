#include "project/project_io.hpp"
#include "editor/editor_history.hpp"
#include <cassert>
#include <limits>

int main() {
    using namespace ProjectIO;
    const Meshes meshes{MeshHandle{11},MeshHandle{22},MeshHandle{33},MeshHandle{44},MeshHandle{55},MeshHandle{66}};
    World world;
    Entity deleted=world.create();world.destroy(deleted);
    Entity parent=world.create(),child=world.create(),empty=world.create();
    world.add(parent,Transform{{1,2,3}});world.add(parent,ObjectName{"Parent \"quoted\"\nname"});
    world.add(child,Transform{{4,5,6}});world.add(child,ObjectName{"Child"});
    world.add(child,MeshRenderer{meshes[5],{0.1f,0.2f,0.3f},true,0.4f});
    Collider c;c.shape=ColliderShape::Cone;c.radius=0.7f;c.height=2;world.add(child,c);
    RigidBody b;b.mass=3;b.useGravity=false;b.linearVelocity={1,2,3};world.add(child,b);
    assert(setParent(world,child,parent));
    OrbitCamera camera;camera.distance=42;camera.target={7,8,9};
    const std::string script="-- source\nprint(\"hello\\world\")\n";
    const auto text=encode(world,script,camera,meshes);
    auto json=parseJson(text);assert(json.at("format")=="LegacyEngine" && json.at("version")==9 && json.at("objects").is_array());
    assert(json.at("objects")[0].at("components").contains("Transform"));
    Loaded legacy;decode(encodeLegacy(world,script,camera,meshes),legacy,meshes);assert(legacy.source==script);
    Loaded loaded;decode(text,loaded,meshes);
    assert(loaded.source==script && loaded.camera.distance==42);
    assert(encode(loaded.world,loaded.source,loaded.camera,meshes)==text);
    assert(loaded.world.alive(empty));
    Meshes remapped=meshes;remapped[5]=MeshHandle{999};
    Loaded portable;decode(text,portable,remapped);
    portable.world.each<MeshRenderer>([&](Entity,MeshRenderer& renderer){assert(renderer.mesh.id==999);});
    const auto path=((std::filesystem::temp_directory_path()/
        ("legacy-project-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))/"Project.legacy").string();
    std::string error,source;OrbitCamera view;
    assert(save(path,world,script,camera,meshes,error));
    auto savedJson=readJsonFile(path,maxBytes);assert(savedJson.at("assets")=="Assets" && !savedJson.contains("editorCamera") && savedJson.at("projectSettings")=="Settings/ProjectSettings.json");
    auto settings=readJsonFile(std::filesystem::path(path).parent_path()/"Settings"/"ProjectSettings.json",64*1024);assert(settings.at("editorCamera").at("distance")==42);
    World other;other.add(other.create(),ObjectName{"Replace me"});
    assert(load(path,other,source,view,meshes,error));
    assert(encode(other,source,view,meshes)==text);
    auto signature=sceneSignature(other);
    // Invalid/truncated input must not replace an existing scene.
    {std::ofstream out(path);out<<"LegacyEngineProject 1\ntruncated";}
    assert(!load(path,other,source,view,meshes,error));
    assert(sceneSignature(other)==signature && source==script);
    assert(!load(path+".missing",other,source,view,meshes,error));
    auto reject=[&](std::string bad){Loaded trial;bool failed=false;try{decode(bad,trial,meshes);}catch(...){failed=true;}assert(failed);};
    auto invalidJson=json;invalidJson["objects"][0]["parent"]=4294967296ULL;reject(invalidJson.dump());
    invalidJson=json;invalidJson["objects"][0]["components"]["Transform"]["position"]={1,2};reject(invalidJson.dump());
    reject("LegacyEngineProject 9\n");reject(text.substr(0,text.size()/2));reject(text+"junk");
    // Parent cycle, invalid mesh and non-finite numeric values.
    reject("LegacyEngineProject 1\n\"\"\n0 0 0 0 0 10 1\n2\n2 1 0 0 0 0 0 0 1 1 1 1\n2 0 0 0 0 0 0 0 1 1 1 1\n");
    world.get<Transform>(parent).position.x=std::numeric_limits<float>::infinity();
    assert(!save(path,world,script,camera,meshes,error));
    // Failed save left the original file intact.
    {std::ifstream in(path);std::string header;std::getline(in,header);assert(header=="LegacyEngineProject 1");}
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());
    {
        World activeWorld;auto p=activeWorld.create(),c=activeWorld.create();
        activeWorld.add(p,Transform{});activeWorld.add(c,Transform{});assert(setParent(activeWorld,c,p));
        setObjectActive(activeWorld,p,false);assert(!isActive(activeWorld,p) && !isActive(activeWorld,c));
        auto encoded=encode(activeWorld,"",OrbitCamera{},meshes);Loaded restored;decode(encoded,restored,meshes);
        for(auto entity:restored.world.entities())assert(!isActive(restored.world,entity));
        Entity selection=p;EditorHistory h(activeWorld,p);setObjectActive(activeWorld,p,true);h.commit(activeWorld,p);
        assert(isActive(activeWorld,c));assert(h.undo(activeWorld,selection));assert(!isActive(activeWorld,c));
        assert(h.redo(activeWorld,selection));assert(isActive(activeWorld,c));
    }

}
