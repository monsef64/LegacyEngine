#include "materials/material_io.hpp"
#include "project/project_io.hpp"
#include "editor/editor_history.hpp"
#include "editor/play_mode.hpp"
#include "scripting/lua_console.hpp"
#include "scripting/script_exports.hpp"
#include <cassert>
int main(){
    const auto root=std::filesystem::temp_directory_path()/("legacy-material-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AssetManager assets;assets.root=root;auto path=createMaterial(assets);assert(path=="NewMaterial.mat");assert(createMaterial(assets)=="NewMaterial (2).mat");
    auto p=loadMaterial(assets,path);assert(p.shader==0 && p.color.x==1 && p.texture.empty());
    p.shader=1;p.texture="Textures/color.png";p.normalMap="Textures/normal.tga";p.color={.3f,.5f,.7f};p.tilingY=2;saveMaterial(assets,path,p);
    auto loaded=loadMaterial(assets,path);assert(loaded.normalMap==p.normalMap && loaded.shader==1 && loaded.tilingY==2);
    p.shader=2;saveMaterial(assets,"Transparent.mat",p);assert(loadMaterial(assets,"Transparent.mat").shader==2);
    MaterialAssetCache cache;assert(cache.get(assets,path).color.x==.3f);
    bool rejected=false;try{loadMaterial(assets,"../outside.mat");}catch(...){rejected=true;}assert(rejected);
    auto malformed=materialJson(p);malformed["shader"]="Unknown";rejected=false;try{parseMaterial(malformed);}catch(...){rejected=true;}assert(rejected);
    malformed=materialJson(p);malformed["normalMap"]="../normal.png";rejected=false;try{parseMaterial(malformed);}catch(...){rejected=true;}assert(rejected);
    World world;auto e=world.create();world.add(e,Transform{});MeshRenderer r;r.material=path;world.add(e,r);
    LuaConsole console;console.bindWorld(world);console.bindObject(e);
    assert(console.execute(R"(local m=self:getComponent(MeshRenderer)
        m:setMaterialColor(0.2,0.4,0.6);m:setMaterialTexture('Textures/new.png');m:setMaterialNormalMap('Textures/newnormal.png')
        m:setMaterialShader('Bumped Diffuse');m:setMaterialTiling(2,3);m:setMaterialNormalStrength(0.5);m:setMaterialSpecular(0.25)
    )"));
    auto& renderer=world.get<MeshRenderer>(e);assert(renderer.materialOverrides==127);assert(renderer.materialValues.tilingY==3);
    assert(loadMaterial(assets,path).texture=="Textures/color.png"); // instance edits never write asset
    assert(!console.execute("self:getComponent(MeshRenderer):setMaterialTexture('../escape.png')"));
    assert(!console.execute("self:getComponent(MeshRenderer):setMaterialTiling(0,1)"));
    assert(!console.execute("self:getComponent(MeshRenderer):setMaterial('bad.png')"));
    assert(console.execute("self:getComponent(MeshRenderer):setMaterialShader('Diffuse Transparent')"));assert(renderer.materialValues.shader==2);
    auto signature=sceneSignature(world);renderer.materialValues.color.x=.4f;assert(sceneSignature(world)!=signature);
    ProjectIO::Meshes meshes{};ProjectIO::Loaded project;auto encoded=ProjectIO::encode(world,"",OrbitCamera{},meshes);ProjectIO::decode(encoded,project,meshes);
    const auto& restored=project.world.get<MeshRenderer>(e);assert(restored.material==path && restored.materialOverrides==127 && restored.materialValues.color.x==.4f);
    auto older=parseJson(encoded);auto& fields=older["objects"][0]["components"]["MeshRenderer"];fields.erase("material");fields.erase("materialOverrides");fields.erase("materialValues");
    ProjectIO::Loaded oldProject;ProjectIO::decode(older.dump(),oldProject,meshes);assert(oldProject.world.get<MeshRenderer>(e).material.empty());
    PlayMode play;play.play(world);world.get<MeshRenderer>(e).material.clear();play.stop(world);assert(world.get<MeshRenderer>(e).material==path);
    std::vector<ScriptField> exports;syncScriptExports("@export albedo Texture \"Textures/color.png\"",exports);assert(exports.size()==1 && exports[0].type=="Texture");
    assert(console.bindExports(exports));assert(console.execute("assert(albedo=='Textures/color.png');self:getComponent(MeshRenderer):setMaterialTexture(albedo)"));
    assert(console.execute("self:getComponent(MeshRenderer):resetMaterialOverrides()"));assert(world.get<MeshRenderer>(e).materialOverrides==0);
    assert(console.execute("self:getComponent(MeshRenderer).material='Other.mat'"));assert(world.get<MeshRenderer>(e).material=="Other.mat");
    console.bindWorld(world,{10},{11},{12},{13},{14});console.bindObject(e);
    assert(console.execute("self:getComponent(MeshRenderer).mesh='Plane';assert(self:getComponent(MeshRenderer).mesh=='Plane')"));assert(world.get<MeshRenderer>(e).mesh.id==14);
    assert(console.execute("stale=self:getComponent(MeshRenderer);self:removeComponent(MeshRenderer)"));assert(!console.execute("stale:setMaterialColor(1,1,1)"));
    std::filesystem::remove_all(root);
}
