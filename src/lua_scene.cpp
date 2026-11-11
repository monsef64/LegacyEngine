#include "lua_scene.hpp"
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
const char* types[]{"Transform","RigidBody","Collider","MeshRenderer","ObjectName"};
int componentType(lua_State* L,int index) {
    const char* name=luaL_checkstring(L,index);
    if (!std::strcmp(name,"BoxCollider")) return 2;
    for (int i=0;i<5;++i) if (!std::strcmp(name,types[i])) return i;
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
    default:return h.world->has<ObjectName>(h.entity);
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
void component(lua_State* L,const Handle& h,int type) {
    auto* c=static_cast<Component*>(lua_newuserdatauv(L,sizeof(Component),0));
    *c={h,type};luaL_setmetatable(L,"Component");
}
int trace(lua_State* L) {
    luaL_traceback(L,L,lua_type(L,1)==LUA_TSTRING ? lua_tostring(L,1) : "Lua callback error",1);return 1;
}
}
LuaScene::LuaScene(lua_State* L,World& world,MeshHandle cube,MeshHandle sphere,MeshHandle capsule,
                   std::function<void(std::string_view)> error)
    :state_(L),world_(world),meshes_{cube,sphere,capsule},error_(std::move(error)) {
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
    const char* names[]{"createGameObject","destroyGameObject","getComponent","addComponent","removeComponent","findGameObjectByName","findGameObjectsByComponent"};
    for(int i=0;i<7;++i) {
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
        } return 0;
    }
    if(!has(h,type)) switch(type) {
    case 0:self.world_.add(h.entity,Transform{});break;
    case 1:self.world_.add(h.entity,RigidBody{});break;
    case 2:self.world_.add(h.entity,Collider{});break;
    case 3:self.world_.add(h.entity,MeshRenderer{self.meshes_[0]});break;
    case 4:self.world_.add(h.entity,ObjectName{"GameObject"});break;
    }
    component(L,h,type);return 1;
}
int LuaScene::objectIndex(lua_State* L) {
    auto& h=handle(L,1);const char* key=luaL_checkstring(L,2);
    if(!std::strcmp(key,"name")) {auto* n=h.world->tryGet<ObjectName>(h.entity);lua_pushstring(L,n?n->value.c_str():"");return 1;}
    lua_getiuservalue(L,1,1);lua_pushvalue(L,2);lua_rawget(L,-2);return 1;
}
int LuaScene::objectNewIndex(lua_State* L) {
    auto& h=handle(L,1);const char* key=luaL_checkstring(L,2);
    if(!std::strcmp(key,"name")) {const char* n=objectName(L,3);h.world->add(h.entity,ObjectName{n});return 0;}
    lua_getiuservalue(L,1,1);lua_pushvalue(L,2);lua_pushvalue(L,3);lua_rawset(L,-3);return 0;
}
int LuaScene::componentIndex(lua_State* L) {
    const auto c=*static_cast<Component*>(luaL_checkudata(L,1,"Component"));const char* key=luaL_checkstring(L,2);
    const Field f=field(L,c,key);
    switch(f.kind) {
    case Kind::Number:lua_pushnumber(L,*static_cast<float*>(f.value));break;
    case Kind::Boolean:lua_pushboolean(L,*static_cast<bool*>(f.value));break;
    case Kind::String:lua_pushstring(L,static_cast<std::string*>(f.value)->c_str());break;
    case Kind::Shape:lua_pushstring(L,(*static_cast<ColliderShape*>(f.value)==ColliderShape::Box)?"Box":(*static_cast<ColliderShape*>(f.value)==ColliderShape::Sphere)?"Sphere":"Capsule");break;
    case Kind::Mesh:{const auto m=*static_cast<MeshHandle*>(f.value);const auto& meshes=c.object.scene->meshes_;lua_pushstring(L,m.id==meshes[1].id?"Sphere":m.id==meshes[2].id?"Capsule":"Cube");break;}
    default:{auto* v=static_cast<Vector*>(lua_newuserdatauv(L,sizeof(Vector),0));v->component=c;std::strncpy(v->field,key,31);v->field[31]=0;luaL_setmetatable(L,"Vector");break;}
    }return 1;
}
int LuaScene::componentNewIndex(lua_State* L) {
    const auto c=*static_cast<Component*>(luaL_checkudata(L,1,"Component"));const Field f=field(L,c,luaL_checkstring(L,2));
    switch(f.kind) {
    case Kind::Number:*static_cast<float*>(f.value)=number(L,3);break;
    case Kind::Boolean:luaL_checktype(L,3,LUA_TBOOLEAN);*static_cast<bool*>(f.value)=lua_toboolean(L,3)!=0;break;
    case Kind::String:*static_cast<std::string*>(f.value)=objectName(L,3);break;
    case Kind::Shape:{const char* s=luaL_checkstring(L,3);int i=!std::strcmp(s,"Box")?0:!std::strcmp(s,"Sphere")?1:!std::strcmp(s,"Capsule")?2:-1;if(i<0)return luaL_error(L,"Expected Box, Sphere or Capsule");*static_cast<ColliderShape*>(f.value)=static_cast<ColliderShape>(i);break;}
    case Kind::Mesh:{const char* s=luaL_checkstring(L,3);int i=!std::strcmp(s,"Cube")?0:!std::strcmp(s,"Sphere")?1:!std::strcmp(s,"Capsule")?2:-1;if(i<0)return luaL_error(L,"Expected Cube, Sphere or Capsule");*static_cast<MeshHandle*>(f.value)=c.object.scene->meshes_[i];break;}
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
void LuaScene::update(float dt){tick("onUpdate",dt);}
void LuaScene::fixedUpdate(float dt){tick("onFixedUpdate",dt);}
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
