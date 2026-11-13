#include "scripting/lua_scene.hpp"
#include "core/hierarchy.hpp"
#include "scripting/script_exports.hpp"
#include "scene/scene_camera.hpp"
#include <cstdio>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <cmath>
#include <cstring>

namespace {
struct Handle { LuaScene* scene; World* world; Entity entity; };
struct Component { Handle object; int type; };
struct Vector { Component component; char field[32]; };
const char* types[]{"Transform","RigidBody","Collider","MeshRenderer","ObjectName","LuaScript","Camera","AudioSource","AudioListener"};
int componentType(lua_State* L,int index) {
    if (auto* type=static_cast<int*>(luaL_testudata(L,index,"ComponentType"))) return *type;
    const char* name=luaL_checkstring(L,index);
    if (!std::strcmp(name,"BoxCollider")) return 2;
    for (int i=0;i<9;++i) if (!std::strcmp(name,types[i])) return i;
    return luaL_error(L,"Unknown component type: %s",name);
}
Handle& handle(lua_State* L,int index) {
    auto* h=static_cast<Handle*>(luaL_checkudata(L,index,"GameObject"));
    if (!h->world->alive(h->entity)) luaL_error(L,"GameObject has been destroyed");
    return *h;
}
bool has(const Handle& h,int type) {
    switch(type) {
    case 0:return h.world->has<Transform>(h.entity);
    case 1:return h.world->has<RigidBody>(h.entity);
    case 2:return h.world->has<Collider>(h.entity);
    case 3:return h.world->has<MeshRenderer>(h.entity);
    case 4:return h.world->has<ObjectName>(h.entity);
    case 5:return h.world->has<LuaScript>(h.entity);
    case 6:return h.world->has<Camera>(h.entity);
    case 7:return h.world->has<AudioSource>(h.entity);
    default:return h.world->has<AudioListener>(h.entity);
    }
}
enum class Kind { Missing, Number, Boolean, String, Vec, Quat, Shape, Mesh };
struct Field { Kind kind=Kind::Missing; void* value=nullptr; };
Field field(lua_State* L,const Component& c,const char* key) {
    const Handle& h=c.object;
    if (!h.world->alive(h.entity) || !has(h,c.type)) luaL_error(L,"Component no longer exists");
#define FIELD(name,kind) if (!std::strcmp(key,#name)) return {Kind::kind,&v.name}
    switch(c.type) {
    case 0:{auto& v=h.world->get<Transform>(h.entity); FIELD(position,Vec);FIELD(rotation,Quat);FIELD(scale,Vec);break;}
    case 1:{auto& v=h.world->get<RigidBody>(h.entity); FIELD(mass,Number);FIELD(useGravity,Boolean);FIELD(linearVelocity,Vec);FIELD(angularVelocity,Vec);break;}
    case 2:{auto& v=h.world->get<Collider>(h.entity); FIELD(size,Vec);FIELD(center,Vec);FIELD(friction,Number);FIELD(bounciness,Number);FIELD(radius,Number);FIELD(height,Number);FIELD(shape,Shape);break;}
    case 3:{auto& v=h.world->get<MeshRenderer>(h.entity); FIELD(color,Vec);FIELD(checkerboard,Boolean);FIELD(specular,Number);FIELD(mesh,Mesh);break;}
    case 4:{auto& v=h.world->get<ObjectName>(h.entity); FIELD(value,String);break;}
    case 5:{auto& v=h.world->get<LuaScript>(h.entity); FIELD(path,String);FIELD(enabled,Boolean);break;}
    case 8:{auto& v=h.world->get<AudioListener>(h.entity);FIELD(enabled,Boolean);break;}
    case 6:{auto& v=h.world->get<Camera>(h.entity); FIELD(active,Boolean);FIELD(orthographic,Boolean);FIELD(fieldOfView,Number);FIELD(nearClip,Number);FIELD(farClip,Number);FIELD(orthographicSize,Number);break;}
    }
#undef FIELD
    luaL_error(L,"Unknown %s field: %s",types[c.type],key);
    return {};
}
float* coordinate(Field f,int i) {
    if(f.kind==Kind::Quat){auto* q=static_cast<Quat*>(f.value);return i==0?&q->x:i==1?&q->y:i==2?&q->z:&q->w;}
    auto* v=static_cast<Vec3*>(f.value);return i==0?&v->x:i==1?&v->y:&v->z;
}
const char* objectName(lua_State* L,int index) {
    std::size_t size=0;const char* name=luaL_checklstring(L,index,&size);
    if(size>256)luaL_error(L,"Object names are limited to 256 bytes");
    return name;
}
float number(lua_State* L,int index) {
    const double n=luaL_checknumber(L,index);
    if (!std::isfinite(n) || std::abs(n)>1e6) luaL_error(L,"Number must be finite and within +/-1000000");
    return static_cast<float>(n);
}
Vec3 vectorArgument(lua_State* L,int index) {
    if(lua_type(L,index)==LUA_TNUMBER)return {number(L,index),number(L,index+1),number(L,index+2)};
    luaL_checktype(L,index,LUA_TTABLE);Vec3 v;
    lua_getfield(L,index,"x");v.x=number(L,-1);lua_pop(L,1);
    lua_getfield(L,index,"y");v.y=number(L,-1);lua_pop(L,1);
    lua_getfield(L,index,"z");v.z=number(L,-1);lua_pop(L,1);return v;
}
Quat rotationArgument(lua_State* L,int index) {
    const Vec3 v=vectorArgument(L,index);const bool table=lua_istable(L,index);
    if(table)lua_getfield(L,index,"w");
    const int wIndex=table?-1:index+3;
    Quat result;
    if(lua_isnoneornil(L,wIndex))result=composeRotation(composeRotation(axisAngle({0,1,0},radians(v.y)),axisAngle({1,0,0},radians(v.x))),axisAngle({0,0,1},radians(v.z)));
    else {
        const float w=number(L,wIndex);
        if(v.x*v.x+v.y*v.y+v.z*v.z+w*w<1e-12f)luaL_error(L,"Rotation quaternion must not be zero");
        result=normalize(Quat{v.x,v.y,v.z,w});
    }
    if(table)lua_pop(L,1);return result;
}
void pushVector(lua_State* L,Vec3 v) {
    lua_createtable(L,0,3);lua_pushnumber(L,v.x);lua_setfield(L,-2,"x");lua_pushnumber(L,v.y);lua_setfield(L,-2,"y");lua_pushnumber(L,v.z);lua_setfield(L,-2,"z");
}
void component(lua_State* L,const Handle& h,int type) {
    auto* c=static_cast<Component*>(lua_newuserdatauv(L,sizeof(Component),0));
    *c={h,type};luaL_setmetatable(L,"Component");
}
int trace(lua_State* L) {
    luaL_traceback(L,L,lua_type(L,1)==LUA_TSTRING ? lua_tostring(L,1) : "Lua callback error",1);return 1;
}
}
LuaScene::LuaScene(lua_State* L,World& world,MeshHandle cube,MeshHandle sphere,MeshHandle capsule,MeshHandle cone,
                   std::function<void(std::string_view)> error)
    :state_(L),world_(world),meshes_{cube,sphere,capsule,cone},error_(std::move(error)) {
    // Keep lifecycle keys interned even when a script exhausts its allocation budget.
    for(const char* key:{"onStart","onUpdate","onFixedUpdate","onDestroy"}) {
        lua_pushboolean(L,1);lua_setfield(L,LUA_REGISTRYINDEX,key);
    }
    const char* metas[]{"GameObject","Component","Vector"};
    const lua_CFunction reads[]{objectIndex,componentIndex,vectorIndex};
    const lua_CFunction writes[]{objectNewIndex,componentNewIndex,vectorNewIndex};
    for(int i=0;i<3;++i) {
        luaL_newmetatable(L,metas[i]);
        lua_pushcfunction(L,reads[i]);lua_setfield(L,-2,"__index");
        lua_pushcfunction(L,writes[i]);lua_setfield(L,-2,"__newindex");
        lua_pushliteral(L,"locked");lua_setfield(L,-2,"__metatable");lua_pop(L,1);
    }
    // Opaque type tokens cannot be forged by passing a number or a table.
    luaL_newmetatable(L,"ComponentType");
    lua_pushlightuserdata(L,this);lua_pushcclosure(L,componentTypeIndex,1);lua_setfield(L,-2,"__index");
    lua_pushliteral(L,"locked");lua_setfield(L,-2,"__metatable");lua_pop(L,1);
    for (int i=0;i<9;++i) {
        auto* type=static_cast<int*>(lua_newuserdatauv(L,sizeof(int),0));
        *type=i;luaL_setmetatable(L,"ComponentType");
        if(i==2) { lua_pushvalue(L,-1);lua_setglobal(L,"BoxCollider"); }
        lua_setglobal(L,types[i]);
    }
    lua_pushlightuserdata(L,this);lua_pushcclosure(L,audioApi,1);lua_setglobal(L,"createAudioSource");
    lua_pushcfunction(L,+[](lua_State* L)->int{luaL_checkstring(L,1);lua_settop(L,1);return 1;});lua_setglobal(L,"AudioClip");
    const char* cameraFunctions[]{"createCamera","setActiveCamera","getActiveCamera","setCameraPosition","setCameraRotation","lookAt","setFieldOfView","setProjectionMode","setNearClipPlane","setFarClipPlane","setViewport","setCameraTarget","setFollowTarget","setCameraShake","worldToScreen","screenToWorld"};
    for(int i=0;i<16;++i) {
        lua_pushlightuserdata(L,this);lua_pushinteger(L,i);lua_pushcclosure(L,cameraApi,2);lua_setglobal(L,cameraFunctions[i]);
    }
    lua_newtable(L);
    lua_pushlightuserdata(L,this);lua_pushinteger(L,0);lua_pushcclosure(L,api,2);lua_setfield(L,-2,"new");
    lua_pushlightuserdata(L,this);lua_pushinteger(L,5);lua_pushcclosure(L,api,2);lua_setfield(L,-2,"find");
    lua_setglobal(L,"GameObject");
    const char* names[]{"createGameObject","destroyGameObject","getComponent","addComponent","removeComponent","findGameObjectByName","findGameObjectsByComponent","setParent","getParent"};
    for(int i=0;i<9;++i) {
        lua_pushlightuserdata(L,this);lua_pushinteger(L,i);lua_pushcclosure(L,api,2);lua_setglobal(L,names[i]);
    }
}
void LuaScene::pushObject(Entity e,bool owned) {
    for(auto& entry:entries_) if(entry.entity==e) { lua_rawgeti(state_,LUA_REGISTRYINDEX,entry.reference);return; }
    if(entries_.size()>=4096) { luaL_error(state_,"GameObject handle limit (4096) exceeded");return; }
    auto* h=static_cast<Handle*>(lua_newuserdatauv(state_,sizeof(Handle),1));
    *h={this,&world_,e};luaL_setmetatable(state_,"GameObject");
    lua_newtable(state_);lua_setiuservalue(state_,-2,1);
    lua_pushvalue(state_,-1);const int reference=luaL_ref(state_,LUA_REGISTRYINDEX);
    entries_.push_back({e,reference,owned,false,false});
}
int LuaScene::api(lua_State* L) {
    auto& self=*static_cast<LuaScene*>(lua_touserdata(L,lua_upvalueindex(1)));
    const int op=static_cast<int>(lua_tointeger(L,lua_upvalueindex(2)));
    if(op==7 || op==8) {
        const Handle child=handle(L,1);
        if(op==8){const Entity p=getParent(self.world_,child.entity);if(p==Entity{})lua_pushnil(L);else self.pushObject(p);return 1;}
        const Entity parent=lua_isnoneornil(L,2)?Entity{}:handle(L,2).entity;
        if(!setParent(self.world_,child.entity,parent))return luaL_error(L,"Invalid parent: cycle, stale object or missing Transform");
        return 0;
    }
    if(op==0) {
        if(self.stopping_) return luaL_error(L,"Cannot create objects during shutdown");
        if(self.entries_.size()>=4096) return luaL_error(L,"GameObject handle limit exceeded");
        const char* name=objectName(L,1);
        // Complete fallible Lua allocations before adding anything to the ECS.
        self.pushObject({},true);
        const Entity e=self.world_.create();self.world_.add(e,Transform{});self.world_.add(e,ObjectName{name});
        static_cast<Handle*>(lua_touserdata(L,-1))->entity=e;
        self.entries_.back().entity=e;
        return 1;
    }
    if(op==5) {
        const char* name=luaL_checkstring(L,1);Entity found;
        self.world_.each<ObjectName>([&](Entity e,ObjectName& n){if(found==Entity{} && n.value==name) found=e;});
        if(found==Entity{}) lua_pushnil(L);else self.pushObject(found);return 1;
    }
    if(op==6) {
        const int type=componentType(L,1);
        // Keep collection outside Lua calls: an allocation error must not jump over a live vector.
        self.query_.clear();
        // Include objects without Transform as well.
        auto collect=[&](Entity e){self.query_.push_back(e);};
        switch(type){
        case 0:self.world_.each<Transform>([&](Entity e,Transform&){collect(e);});break;
        case 1:self.world_.each<RigidBody>([&](Entity e,RigidBody&){collect(e);});break;
        case 2:self.world_.each<Collider>([&](Entity e,Collider&){collect(e);});break;
        case 3:self.world_.each<MeshRenderer>([&](Entity e,MeshRenderer&){collect(e);});break;
        case 4:self.world_.each<ObjectName>([&](Entity e,ObjectName&){collect(e);});break;
        case 5:self.world_.each<LuaScript>([&](Entity e,LuaScript&){collect(e);});break;
        case 8:self.world_.each<AudioListener>([&](Entity e,AudioListener&){collect(e);});break;
        case 7:self.world_.each<AudioSource>([&](Entity e,AudioSource&){collect(e);});break;
        case 6:self.world_.each<Camera>([&](Entity e,Camera&){collect(e);});break;
        }
        lua_newtable(L);int index=1;
        for(Entity e:self.query_) {self.pushObject(e);lua_rawseti(L,-2,index++);}return 1;
    }
    const Handle h=handle(L,1);
    if(h.scene!=&self) return luaL_error(L,"Foreign GameObject");
    if(op==1) {self.destroy(h.entity);return 0;}
    const int type=componentType(L,2);
    if(op==2) {if(has(h,type)) component(L,h,type);else lua_pushnil(L);return 1;}
    if(op==4) {
        switch(type) {
        case 0:self.world_.remove<Transform>(h.entity);break;
        case 1:self.world_.remove<RigidBody>(h.entity);break;
        case 2:self.world_.remove<Collider>(h.entity);break;
        case 3:self.world_.remove<MeshRenderer>(h.entity);break;
        case 4:self.world_.remove<ObjectName>(h.entity);break;
        case 5:self.world_.remove<LuaScript>(h.entity);break;
        case 6:self.world_.remove<Camera>(h.entity);break;
        case 7:self.world_.remove<AudioSource>(h.entity);break;
        case 8:self.world_.remove<AudioListener>(h.entity);break;
        } return 0;
    }
    if(!has(h,type)) switch(type) {
    case 0:self.world_.add(h.entity,Transform{});break;
    case 1:self.world_.add(h.entity,RigidBody{});break;
    case 2:self.world_.add(h.entity,Collider{});break;
    case 3:self.world_.add(h.entity,MeshRenderer{self.meshes_[0]});break;
    case 4:self.world_.add(h.entity,ObjectName{"GameObject"});break;
    case 5:self.world_.add(h.entity,LuaScript{});break;
    case 6:self.world_.add(h.entity,Camera{});break;
    case 7:self.world_.add(h.entity,AudioSource{});break;
    case 8:if(!self.world_.has<Transform>(h.entity))self.world_.add(h.entity,Transform{});self.world_.add(h.entity,AudioListener{});break;
    }
    component(L,h,type);return 1;
}
// A bound closure supports both obj.getComponent(Type) and obj:getComponent(Type).
int LuaScene::objectMethod(lua_State* L) {
    const Handle bound=handle(L,lua_upvalueindex(3));
    if (luaL_testudata(L,1,"GameObject")) {
        const Handle explicitSelf=handle(L,1);
        const int operation=static_cast<int>(lua_tointeger(L,lua_upvalueindex(2)));
        if(explicitSelf.entity==bound.entity && explicitSelf.scene==bound.scene &&
            !(operation==7 && lua_gettop(L)==1))lua_remove(L,1);
    }
    lua_pushvalue(L,lua_upvalueindex(3));lua_insert(L,1);
    const int operation=static_cast<int>(lua_tointeger(L,lua_upvalueindex(2)));
    if(operation>=200)return objectApi(L);
    if(operation>=100)return cameraApi(L);
    return api(L);
}
int LuaScene::objectIndex(lua_State* L) {
    auto& h=handle(L,1);const char* key=luaL_checkstring(L,2);
    const char* methods[]{"getComponent","addComponent","removeComponent"};
    for(int i=0;i<3;++i) if(!std::strcmp(key,methods[i])) {
        lua_pushlightuserdata(L,h.scene);lua_pushinteger(L,i+2);lua_pushvalue(L,1);
        lua_pushcclosure(L,objectMethod,3);return 1;
    }
    struct Method {const char* name;int operation;};
    const Method objectMethods[]{{"setPosition",200},{"getPosition",201},{"setRotation",202},{"getRotation",203},
        {"setScale",204},{"getScale",205},{"translate",206},{"lookAt",207},{"setParent",7},{"getParent",8},{"destroy",1},
        {"setName",208},{"getName",209}};
    for(const auto& method:objectMethods)if(!std::strcmp(key,method.name)) {
        lua_pushlightuserdata(L,h.scene);lua_pushinteger(L,method.operation);lua_pushvalue(L,1);lua_pushcclosure(L,objectMethod,3);return 1;
    }
    if(h.world->has<Camera>(h.entity)) {
        const Method cameraMethods[]{{"setActive",101},{"setFieldOfView",106},{"setProjectionMode",107},
            {"setNearClipPlane",108},{"setFarClipPlane",109},{"setViewport",110},{"setTarget",111},{"setCameraTarget",111},
            {"setFollowTarget",112},{"setShake",113},{"setCameraShake",113},{"worldToScreen",114},{"screenToWorld",115}};
        for(const auto& method:cameraMethods)if(!std::strcmp(key,method.name)) {
            lua_pushlightuserdata(L,h.scene);lua_pushinteger(L,method.operation);lua_pushvalue(L,1);lua_pushcclosure(L,objectMethod,3);return 1;
        }
    }
    if(!std::strcmp(key,"parent")){const Entity p=getParent(*h.world,h.entity);if(p==Entity{})lua_pushnil(L);else h.scene->pushObject(p);return 1;}
    if(!std::strcmp(key,"name")) {auto* n=h.world->tryGet<ObjectName>(h.entity);lua_pushstring(L,n?n->value.c_str():"");return 1;}
    lua_getiuservalue(L,1,1);lua_pushvalue(L,2);lua_rawget(L,-2);return 1;
}
int LuaScene::objectNewIndex(lua_State* L) {
    auto& h=handle(L,1);const char* key=luaL_checkstring(L,2);
    if(!std::strcmp(key,"parent")){const Entity p=lua_isnil(L,3)?Entity{}:handle(L,3).entity;if(!setParent(*h.world,h.entity,p))return luaL_error(L,"Invalid parent");return 0;}
    if(!std::strcmp(key,"name")) {const char* n=objectName(L,3);h.world->add(h.entity,ObjectName{n});return 0;}
    lua_getiuservalue(L,1,1);lua_pushvalue(L,2);lua_pushvalue(L,3);lua_rawset(L,-3);return 0;
}
int LuaScene::componentIndex(lua_State* L) {
    const auto c=*static_cast<Component*>(luaL_checkudata(L,1,"Component"));const char* key=luaL_checkstring(L,2);
    if(c.type==8 && (!std::strcmp(key,"pause") || !std::strcmp(key,"start"))) {
        if(!c.object.world->alive(c.object.entity) || !c.object.world->has<AudioListener>(c.object.entity))return luaL_error(L,"AudioListener no longer exists");
        lua_pushvalue(L,1);lua_pushboolean(L,!std::strcmp(key,"start"));lua_pushcclosure(L,listenerMethod,2);return 1;
    }
    if(c.type==7){
        if(!c.object.world->alive(c.object.entity) || !c.object.world->has<AudioSource>(c.object.entity))return luaL_error(L,"AudioSource no longer exists");
        const char* names[]{"play","stop","pause","resume","setPitch","fadeIn","fadeOut","setVolume","mute","unmute","setLooping","setPan","setDistance","isPlaying","playOneShot","spatialBlend","setSpatialBlend"};
        for(int i=0;i<17;++i)if(!std::strcmp(key,names[i])){lua_pushvalue(L,1);lua_pushinteger(L,i);lua_pushcclosure(L,audioMethod,2);return 1;}
        auto& a=c.object.world->get<AudioSource>(c.object.entity);
        if(!std::strcmp(key,"clip")){lua_pushstring(L,a.clip.c_str());return 1;}
        lua_pushnil(L);return 1;
    }
    const Field f=field(L,c,key);
    switch(f.kind) {
    case Kind::Number:lua_pushnumber(L,*static_cast<float*>(f.value));break;
    case Kind::Boolean:lua_pushboolean(L,*static_cast<bool*>(f.value));break;
    case Kind::String:lua_pushstring(L,static_cast<std::string*>(f.value)->c_str());break;
    case Kind::Shape:lua_pushstring(L,(*static_cast<ColliderShape*>(f.value)==ColliderShape::Box)?"Box":(*static_cast<ColliderShape*>(f.value)==ColliderShape::Sphere)?"Sphere":(*static_cast<ColliderShape*>(f.value)==ColliderShape::Capsule)?"Capsule":"Cone");break;
    case Kind::Mesh:{const auto m=*static_cast<MeshHandle*>(f.value);const auto& meshes=c.object.scene->meshes_;lua_pushstring(L,m.id==meshes[1].id?"Sphere":m.id==meshes[2].id?"Capsule":m.id==meshes[3].id?"Cone":"Cube");break;}
    default:{auto* v=static_cast<Vector*>(lua_newuserdatauv(L,sizeof(Vector),0));v->component=c;std::strncpy(v->field,key,31);v->field[31]=0;luaL_setmetatable(L,"Vector");break;}
    }return 1;
}
int LuaScene::componentNewIndex(lua_State* L) {
    const auto c=*static_cast<Component*>(luaL_checkudata(L,1,"Component"));
    if(c.type==7){if(!c.object.world->alive(c.object.entity) || !c.object.world->has<AudioSource>(c.object.entity))return luaL_error(L,"AudioSource no longer exists");
        if(!std::strcmp(luaL_checkstring(L,2),"clip")){c.object.world->get<AudioSource>(c.object.entity).clip=luaL_checkstring(L,3);return 0;}return luaL_error(L,"Use AudioSource setter methods");}
    const Field f=field(L,c,luaL_checkstring(L,2));
    if(c.type==6 && !std::strcmp(lua_tostring(L,2),"active")) {
        luaL_checktype(L,3,LUA_TBOOLEAN);
        if(lua_toboolean(L,3)) {
            if(!c.object.world->has<Transform>(c.object.entity))return luaL_error(L,"Camera needs a Transform");
            activateCamera(*c.object.world,c.object.entity);
        } else c.object.world->get<Camera>(c.object.entity).active=false;
        return 0;
    }
    switch(f.kind) {
    case Kind::Number:*static_cast<float*>(f.value)=number(L,3);break;
    case Kind::Boolean:luaL_checktype(L,3,LUA_TBOOLEAN);*static_cast<bool*>(f.value)=lua_toboolean(L,3)!=0;break;
    case Kind::String:*static_cast<std::string*>(f.value)=objectName(L,3);break;
    case Kind::Shape:{const char* s=luaL_checkstring(L,3);int i=!std::strcmp(s,"Box")?0:!std::strcmp(s,"Sphere")?1:!std::strcmp(s,"Capsule")?2:!std::strcmp(s,"Cone")?3:-1;if(i<0)return luaL_error(L,"Expected Box, Sphere, Capsule or Cone");*static_cast<ColliderShape*>(f.value)=static_cast<ColliderShape>(i);break;}
    case Kind::Mesh:{const char* s=luaL_checkstring(L,3);int i=!std::strcmp(s,"Cube")?0:!std::strcmp(s,"Sphere")?1:!std::strcmp(s,"Capsule")?2:!std::strcmp(s,"Cone")?3:-1;if(i<0)return luaL_error(L,"Expected Cube, Sphere, Capsule or Cone");*static_cast<MeshHandle*>(f.value)=c.object.scene->meshes_[i];break;}
    default:{
        float values[4]{};const char* keys[]{"x","y","z","w"};const int count=f.kind==Kind::Quat?4:3;
        for(int i=0;i<count;++i){lua_getfield(L,3,keys[i]);values[i]=number(L,-1);lua_pop(L,1);}
        if(count==4)*static_cast<Quat*>(f.value)=normalize(Quat{values[0],values[1],values[2],values[3]});
        else *static_cast<Vec3*>(f.value)={values[0],values[1],values[2]};
        break;
    }}return 0;
}
int LuaScene::vectorIndex(lua_State* L) {
    const auto v=*static_cast<Vector*>(luaL_checkudata(L,1,"Vector"));const Field f=field(L,v.component,v.field);const char* key=luaL_checkstring(L,2);
    const int i=!std::strcmp(key,"x")?0:!std::strcmp(key,"y")?1:!std::strcmp(key,"z")?2:(f.kind==Kind::Quat && !std::strcmp(key,"w"))?3:-1;
    if(i<0)return luaL_error(L,"Unknown vector coordinate");
    lua_pushnumber(L,*coordinate(f,i));return 1;
}
int LuaScene::vectorNewIndex(lua_State* L) {
    const auto v=*static_cast<Vector*>(luaL_checkudata(L,1,"Vector"));const Field f=field(L,v.component,v.field);const char* key=luaL_checkstring(L,2);
    const int i=!std::strcmp(key,"x")?0:!std::strcmp(key,"y")?1:!std::strcmp(key,"z")?2:(f.kind==Kind::Quat && !std::strcmp(key,"w"))?3:-1;
    if(i<0)return luaL_error(L,"Unknown vector coordinate");
    *coordinate(f,i)=number(L,3);return 0;
}
void LuaScene::call(const char* name,int reference,float dt,bool delta) {
    const int base=lua_gettop(state_);lua_pushcfunction(state_,trace);
    if(reference==LUA_NOREF)lua_getglobal(state_,name);
    else {lua_rawgeti(state_,LUA_REGISTRYINDEX,reference);lua_getiuservalue(state_,-1,1);lua_getfield(state_,-1,name);lua_remove(state_,-2);lua_remove(state_,-2);}
    if(!lua_isfunction(state_,-1)){lua_settop(state_,base);return;}
    if(delta)lua_pushnumber(state_,dt);
    if(lua_pcall(state_,delta?1:0,0,base+1)!=LUA_OK) {const char* e=lua_tostring(state_,-1);error_(e?e:"Lua callback error");active_=false;}
    lua_settop(state_,base);
}
void LuaScene::start(){started_=true;active_=true;call("onStart",LUA_NOREF,0,false);}
void LuaScene::destroy(Entity e) {
    for(std::size_t i=0;i<entries_.size();++i)if(entries_[i].entity==e){
        if(entries_[i].destroying)return;
        entries_[i].destroying=true;const int ref=entries_[i].reference;call("onDestroy",ref,0,false);break;
    }
    world_.destroy(e);
}
void LuaScene::tick(const char* name,float dt) {
    // Snapshot count: objects created by callbacks start on the next tick.
    const std::size_t count=entries_.size();
    for(std::size_t i=0;i<count;++i) {
        if(!world_.alive(entries_[i].entity) && !entries_[i].destroying){entries_[i].destroying=true;call("onDestroy",entries_[i].reference,0,false);}
    }
    if(active_) {
        for(std::size_t i=0;i<count && active_;++i) {
            if(entries_[i].destroying || !world_.alive(entries_[i].entity) || entries_[i].started)continue;
            entries_[i].started=true;call("onStart",entries_[i].reference,0,false);
        }
        if(active_)call(name,LUA_NOREF,dt,true);
        for(std::size_t i=0;i<count && active_;++i) {
            if(entries_[i].destroying || !world_.alive(entries_[i].entity))continue;
            call(name,entries_[i].reference,dt,true);
        }
    }
    for(std::size_t i=0;i<entries_.size();) {
        if(!world_.alive(entries_[i].entity)) {
            luaL_unref(state_,LUA_REGISTRYINDEX,entries_[i].reference);
            entries_.erase(entries_.begin()+static_cast<std::ptrdiff_t>(i));
        } else ++i;
    }
}
void LuaScene::update(float dt){tick("onUpdate",dt);updateHierarchy(world_);}
void LuaScene::fixedUpdate(float dt){tick("onFixedUpdate",dt);updateHierarchy(world_);}
void LuaScene::stop() {
    if(stopping_)return;
    stopping_=true;
    if(started_)call("onDestroy",LUA_NOREF,0,false);
    started_=false;
    active_=false;
    for(std::size_t i=0;i<entries_.size();++i){
        if(!entries_[i].destroying){entries_[i].destroying=true;call("onDestroy",entries_[i].reference,0,false);}
        if(entries_[i].owned)world_.destroy(entries_[i].entity);
        luaL_unref(state_,LUA_REGISTRYINDEX,entries_[i].reference);
    }
    entries_.clear();stopping_=false;
}

void LuaScene::setSelf(Entity entity) { pushObject(entity);lua_setglobal(state_,"self"); }

#include "scripting/lua_camera.inc"

#include "scripting/lua_objects.inc"

int LuaScene::audioApi(lua_State* L) {
    auto& self=*static_cast<LuaScene*>(lua_touserdata(L,lua_upvalueindex(1)));
    if(lua_gettop(L)==0){lua_getglobal(L,"self");if(lua_isnil(L,-1))return luaL_error(L,"createAudioSource requires an owner GameObject");}
    const auto h=handle(L,1);if(h.world!=&self.world_)return luaL_error(L,"Foreign GameObject");
    if(!self.world_.has<AudioSource>(h.entity))self.world_.add(h.entity,AudioSource{});
    if(!lua_isnoneornil(L,2))self.world_.get<AudioSource>(h.entity).clip=luaL_checkstring(L,2);
    component(L,h,7);return 1;
}
int LuaScene::listenerMethod(lua_State* L){
    const auto c=*static_cast<Component*>(luaL_checkudata(L,lua_upvalueindex(1),"Component"));
    if(!c.object.world->alive(c.object.entity) || !c.object.world->has<AudioListener>(c.object.entity))return luaL_error(L,"AudioListener no longer exists");
    auto& listener=c.object.world->get<AudioListener>(c.object.entity);
    if(lua_toboolean(L,lua_upvalueindex(2)))listener.start();else listener.pause();
    return 0;
}
int LuaScene::audioMethod(lua_State* L){
    const auto c=*static_cast<Component*>(luaL_checkudata(L,lua_upvalueindex(1),"Component"));
    if(!c.object.world->alive(c.object.entity) || !c.object.world->has<AudioSource>(c.object.entity))return luaL_error(L,"AudioSource no longer exists");
    auto& a=c.object.world->get<AudioSource>(c.object.entity);
    if(luaL_testudata(L,1,"Component"))lua_remove(L,1);
    const int op=lua_tointeger(L,lua_upvalueindex(2));
    if(op==14){a.playOneShot(luaL_checkstring(L,1));return 0;}
    if(op==10){luaL_checktype(L,1,LUA_TBOOLEAN);lua_pushboolean(L,a.setLooping(lua_toboolean(L,1)));return 1;}
    float value=0;if(op==4 || op==5 || op==6 || op==7 || op==11 || op==12 || op==15 || op==16){value=luaL_checknumber(L,1);if(!std::isfinite(value))return luaL_error(L,"Expected finite audio value");}
    switch(op){case 0:a.play();break;case 1:a.stop();break;case 2:a.pause();break;case 3:a.resume();break;
    case 4:lua_pushnumber(L,a.setPitch(value));return 1;case 5:a.fadeIn(value);break;case 6:a.fadeOut(value);break;
    case 7:lua_pushnumber(L,a.setVolume(value));return 1;case 8:a.mute();break;case 9:a.unmute();break;
    case 11:lua_pushnumber(L,a.setPan(value));return 1;case 12:lua_pushnumber(L,a.setDistance(value));return 1;
    case 13:lua_pushboolean(L,a.isPlaying());return 1;
    case 15:case 16:lua_pushnumber(L,a.spatialBlend(value));return 1;}
    return 0;
}

void LuaScene::setExports(const std::vector<ScriptField>& fields){
    for(const auto& field:fields){
        if(!exportName(field.name))continue;
        if(field.type=="number")lua_pushnumber(state_,field.number);
        else if(field.type=="boolean")lua_pushboolean(state_,field.boolean);
        else if(field.type=="string" || exportAssetType(field.type))lua_pushlstring(state_,field.text.data(),field.text.size());
        else if(exportHasComponent(world_,field.entity,field.type)){
            if(field.type=="GameObject")pushObject(field.entity);
            else{int type=0;for(;type<9;++type)if(field.type==types[type])break;component(state_,Handle{this,&world_,field.entity},type);}
        }else lua_pushnil(state_);
        lua_setglobal(state_,field.name.c_str());
    }
}
