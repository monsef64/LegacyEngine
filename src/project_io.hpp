#pragma once
#include "hierarchy.hpp"
#include "scene_camera.hpp"
#include "camera.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

// Versioned, portable text format. Meshes use catalog indices, never GPU IDs.
namespace ProjectIO {
using Meshes = std::array<MeshHandle,6>; // Grid, Plane, Cube, Sphere, Capsule, Cone
constexpr std::size_t maxBytes=16*1024*1024, maxObjects=10000, maxScript=64*1024;
inline void require(bool ok) { if(!ok)throw std::runtime_error("Invalid or unsupported project file"); }
inline float number(std::istream& in) {
    float v=0;in>>v;require(bool(in) && std::isfinite(v) && std::abs(v)<=1e6f);return v;
}
inline void writeVec(std::ostream& out,Vec3 v) {out<<v.x<<' '<<v.y<<' '<<v.z<<' ';}
inline Vec3 readVec(std::istream& in) {const float x=number(in),y=number(in),z=number(in);return {x,y,z};}
inline void writeTransform(std::ostream& out,const Transform& t) {
    writeVec(out,t.position);out<<t.rotation.x<<' '<<t.rotation.y<<' '<<t.rotation.z<<' '<<t.rotation.w<<' ';writeVec(out,t.scale);
}
inline Transform readTransform(std::istream& in) {
    Transform t;t.position=readVec(in);
    t.rotation.x=number(in);t.rotation.y=number(in);t.rotation.z=number(in);t.rotation.w=number(in);
    t.scale=readVec(in);return t;
}
inline std::string encode(World& world,std::string_view source,const OrbitCamera& camera,const Meshes& meshes) {
    require(source.size()<=maxScript);
    const auto entities=world.entities();require(entities.size()<=maxObjects);
    std::ostringstream out;out<<std::setprecision(9)<<"LegacyEngineProject 3\n";
    out<<std::quoted(std::string(source))<<'\n';
    writeVec(out,camera.target);out<<camera.yaw<<' '<<camera.pitch<<' '<<camera.distance<<' '<<camera.fieldOfView<<'\n';
    out<<entities.size()<<'\n';
    for(Entity e:entities) {
        unsigned mask=(world.has<ObjectName>(e)?1:0)|(world.has<Transform>(e)?2:0)|
            (world.has<MeshRenderer>(e)?4:0)|(world.has<Collider>(e)?8:0)|(world.has<RigidBody>(e)?16:0)|(world.has<LuaScript>(e)?32:0)|(world.has<Camera>(e)?64:0);
        int parent=-1;const Entity p=getParent(world,e);
        for(std::size_t i=0;i<entities.size();++i)if(entities[i]==p)parent=static_cast<int>(i);
        out<<mask<<' '<<parent<<' ';
        if(auto* n=world.tryGet<ObjectName>(e))out<<std::quoted(n->value)<<' ';
        if(auto* t=world.tryGet<Transform>(e))writeTransform(out,*t);
        if(auto* m=world.tryGet<MeshRenderer>(e)) {
            int mesh=-1;for(int i=0;i<6;++i)if(meshes[i].id==m->mesh.id)mesh=i;
            require(mesh>=0);out<<mesh<<' ';writeVec(out,m->color);out<<m->checkerboard<<' '<<m->specular<<' ';
        }
        if(auto* c=world.tryGet<Collider>(e)) {
            writeVec(out,c->size);writeVec(out,c->center);
            out<<c->friction<<' '<<c->bounciness<<' '<<static_cast<int>(c->shape)<<' '<<c->radius<<' '<<c->height<<' ';
        }
        if(auto* b=world.tryGet<RigidBody>(e)) {
            out<<b->mass<<' '<<b->useGravity<<' ';writeVec(out,b->linearVelocity);writeVec(out,b->angularVelocity);
        }
        if(auto* script=world.tryGet<LuaScript>(e))out<<std::quoted(script->path)<<' '<<script->enabled<<' ';
        if(auto* c=world.tryGet<Camera>(e)) {
            require(validCamera(*c));
            int target=-1;for(std::size_t i=0;i<entities.size();++i)if(entities[i]==c->targetEntity)target=static_cast<int>(i);
            out<<c->active<<' '<<c->orthographic<<' '<<c->fieldOfView<<' '<<c->nearClip<<' '<<c->farClip<<' '<<c->orthographicSize<<' '
                <<c->viewportX<<' '<<c->viewportY<<' '<<c->viewportWidth<<' '<<c->viewportHeight<<' '<<target<<' ';
            writeVec(out,c->target);writeVec(out,c->followOffset);out<<c->tracking<<' '<<c->following<<' ';
        }
        out<<'\n';
    }
    auto text=out.str();require(text.size()<=maxBytes);return text;
}
struct Loaded { World world; std::string source; OrbitCamera camera; };
inline void decode(const std::string& text,Loaded& result,const Meshes& meshes) {
    require(text.size()<=maxBytes);
    std::istringstream in(text);std::string magic;int version=0;
    in>>magic>>version;require(magic=="LegacyEngineProject" && (version>=1 && version<=3));
    in>>std::quoted(result.source);require(result.source.size()<=maxScript && result.source.find('\0')==std::string::npos);
    auto& camera=result.camera;camera.target=readVec(in);camera.yaw=number(in);camera.pitch=number(in);
    camera.distance=number(in);camera.fieldOfView=number(in);
    require(camera.distance>0 && camera.fieldOfView>0 && camera.fieldOfView<3.14f);
    int count=-1;in>>count;require(bool(in) && count>=0 && count<=static_cast<int>(maxObjects));
    std::vector<Entity> entities;std::vector<int> parents,cameraTargets;
    for(int i=0;i<count;++i) {
        unsigned mask=0;int parent=-1;in>>mask>>parent;
        require(bool(in) && mask<=(version==1?31u:version==2?63u:127u) && parent>=-1 && parent<count && parent!=i);
        const Entity e=result.world.create();entities.push_back(e);parents.push_back(parent);cameraTargets.push_back(-1);
        if(mask&1) {std::string name;in>>std::quoted(name);require(name.size()<=4096);result.world.add(e,ObjectName{name});}
        if(mask&2)result.world.add(e,readTransform(in));
        if(mask&4) {
            int mesh=-1;in>>mesh;require(mesh>=0 && mesh<6);
            MeshRenderer m;m.mesh=meshes[mesh];m.color=readVec(in);in>>m.checkerboard;m.specular=number(in);result.world.add(e,m);
        }
        if(mask&8) {
            Collider c;c.size=readVec(in);c.center=readVec(in);c.friction=number(in);c.bounciness=number(in);
            int shape=-1;in>>shape;require(shape>=0 && shape<4);c.shape=static_cast<ColliderShape>(shape);
            c.radius=number(in);c.height=number(in);require(c.radius>0 && c.height>=0);result.world.add(e,c);
        }
        if(mask&16) {
            RigidBody b;b.mass=number(in);in>>b.useGravity;b.linearVelocity=readVec(in);b.angularVelocity=readVec(in);
            require(b.mass>=0);result.world.add(e,b);
        }
        if(mask&32) {
            LuaScript script;in>>std::quoted(script.path)>>script.enabled;
            require(script.path.size()<=4096 && script.path.find('\0')==std::string::npos && !std::filesystem::path(script.path).is_absolute());
            for(const auto& part:std::filesystem::path(script.path))require(part!="..");
            result.world.add(e,script);
        }
        if(mask&64) {
            require((mask&2)!=0);Camera c;
            in>>c.active>>c.orthographic;c.fieldOfView=number(in);c.nearClip=number(in);c.farClip=number(in);c.orthographicSize=number(in);
            c.viewportX=number(in);c.viewportY=number(in);c.viewportWidth=number(in);c.viewportHeight=number(in);
            in>>cameraTargets.back();require(cameraTargets.back()>=-1 && cameraTargets.back()<count && cameraTargets.back()!=i);
            c.target=readVec(in);c.followOffset=readVec(in);in>>c.tracking>>c.following;require(validCamera(c));result.world.add(e,c);
        }
        require(bool(in));
    }
    for(int i=0;i<count;++i)if(cameraTargets[i]>=0) {
        require(result.world.has<Transform>(entities[cameraTargets[i]]));
        result.world.get<Camera>(entities[i]).targetEntity=entities[cameraTargets[i]];
    }
    int activeCount=0;result.world.each<Camera>([&](Entity,Camera& c){if(c.active)++activeCount;});require(activeCount<=1);
    for(int i=0;i<count;++i)if(parents[i]>=0)require(setParent(result.world,entities[i],entities[parents[i]]));
    in>>std::ws;require(in.eof());
}
inline bool save(const std::string& path,World& world,std::string_view source,const OrbitCamera& camera,const Meshes& meshes,std::string& error) {
    std::filesystem::path temporary;
    try {
        auto text=encode(world,source,camera,meshes);
        Loaded check;decode(text,check,meshes); // Never replace a project with invalid values.
        temporary=path+".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        if(!out)throw std::runtime_error("Could not create project file");
        out.write(text.data(),static_cast<std::streamsize>(text.size()));out.close();
        if(!out)throw std::runtime_error("Could not write project file");
        std::filesystem::rename(temporary,path);error.clear();return true;
    } catch(const std::exception& e) {
        error=e.what();if(!temporary.empty()){std::error_code ec;std::filesystem::remove(temporary,ec);}return false;
    }
}
inline bool load(const std::string& path,World& world,std::string& source,OrbitCamera& camera,const Meshes& meshes,std::string& error) {
    try {
        std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Could not open project file");
        in.seekg(0,std::ios::end);auto size=in.tellg();require(size>=0 && size<=static_cast<std::streamoff>(maxBytes));
        in.seekg(0);std::string text(static_cast<std::size_t>(size),'\0');in.read(text.data(),size);require(bool(in));
        Loaded loaded;decode(text,loaded,meshes);
        world.restore(loaded.world);source=std::move(loaded.source);camera=loaded.camera;error.clear();return true;
    } catch(const std::exception& e) { error=e.what();return false; }
}
}
