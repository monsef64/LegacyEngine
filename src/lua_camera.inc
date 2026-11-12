// Included by lua_scene.cpp to share its checked generation-safe handle helpers.
int LuaScene::cameraApi(lua_State* L) {
    auto& self=*static_cast<LuaScene*>(lua_touserdata(L,lua_upvalueindex(1)));
    const int op=static_cast<int>(lua_tointeger(L,lua_upvalueindex(2)));
    auto vector=[&](int index)->Vec3 {
        luaL_checktype(L,index,LUA_TTABLE);Vec3 v;
        lua_getfield(L,index,"x");v.x=number(L,-1);lua_pop(L,1);
        lua_getfield(L,index,"y");v.y=number(L,-1);lua_pop(L,1);
        lua_getfield(L,index,"z");v.z=number(L,-1);lua_pop(L,1);return v;
    };
    if(op==0) {
        if(self.stopping_ || self.entries_.size()>=4096)return luaL_error(L,"Cannot create camera now");
        const char* name=objectName(L,1);
        const bool active=activeCamera(self.world_)==Entity{};
        self.pushObject({},true);
        auto entity=self.world_.create();self.world_.add(entity,Transform{});self.world_.add(entity,ObjectName{name});
        Camera camera;camera.active=active;self.world_.add(entity,camera);
        static_cast<Handle*>(lua_touserdata(L,-1))->entity=entity;self.entries_.back().entity=entity;return 1;
    }
    if(op==2) {const auto entity=activeCamera(self.world_);if(entity==Entity{})lua_pushnil(L);else self.pushObject(entity);return 1;}
    if(op==1 && lua_isnil(L,1)) {self.world_.each<Camera>([](Entity,Camera& c){c.active=false;});return 0;}
    const auto h=handle(L,1);
    if(h.scene!=&self || !self.world_.has<Camera>(h.entity) || !self.world_.has<Transform>(h.entity))return luaL_error(L,"Expected a Camera GameObject");
    auto& c=self.world_.get<Camera>(h.entity);auto& t=self.world_.get<Transform>(h.entity);
    char error[256]{};
    try {
        Camera proposed=c;
        switch(op) {
        case 1:activateCamera(self.world_,h.entity);return 0;
        case 3:t.position=vector(2);return 0;
        case 4:{
            const auto v=vector(2);lua_getfield(L,2,"w");
            if(lua_isnil(L,-1))t.rotation=composeRotation(composeRotation(axisAngle({0,1,0},radians(v.y)),axisAngle({1,0,0},radians(v.x))),axisAngle({0,0,1},radians(v.z)));
            else {const float w=number(L,-1);if(v.x*v.x+v.y*v.y+v.z*v.z+w*w<1e-12f)return luaL_error(L,"Rotation quaternion must not be zero");t.rotation=normalize(Quat{v.x,v.y,v.z,w});}
            lua_pop(L,1);c.tracking=false;c.following=false;return 0;
        }
        case 5:case 11:case 12:{
            if(lua_isnil(L,2)){c.tracking=false;c.following=false;c.targetEntity={};return 0;}
            Vec3 target;Entity targetEntity;
            if(luaL_testudata(L,2,"GameObject")) {
                const auto targetHandle=handle(L,2);
                if(targetHandle.entity==h.entity || !self.world_.has<Transform>(targetHandle.entity))return luaL_error(L,"Invalid camera target");
                targetEntity=targetHandle.entity;target=self.world_.get<Transform>(targetEntity).position;
            } else target=vector(2);
            const Vec3 offset=op==12 && !lua_isnoneornil(L,3)?vector(3):Vec3{};
            c.target=target;c.targetEntity=targetEntity;c.tracking=op!=5;c.following=op==12;c.followOffset=offset;
            if(c.following)t.position=target+offset;
            aimCamera(t,target);return 0;
        }
        case 6:proposed.fieldOfView=number(L,2);break;
        case 7:{const char* mode=luaL_checkstring(L,2);if((!std::strcmp(mode,"Perspective") || !std::strcmp(mode,"perspective")))proposed.orthographic=false;
            else if((!std::strcmp(mode,"Orthographic") || !std::strcmp(mode,"orthographic")))proposed.orthographic=true;
            else return luaL_error(L,"Expected Perspective or Orthographic");break;}
        case 8:proposed.nearClip=number(L,2);break;
        case 9:proposed.farClip=number(L,2);break;
        case 10:proposed.viewportX=number(L,2);proposed.viewportY=number(L,3);proposed.viewportWidth=number(L,4);proposed.viewportHeight=number(L,5);break;
        case 13:{const float intensity=number(L,2),duration=number(L,3);if(intensity<0 || duration<0)return luaL_error(L,"Shake values must be nonnegative");c.shakeIntensity=intensity;c.shakeRemaining=duration;c.shakePhase=0;return 0;}
        case 14:case 15:{
            const auto point=vector(2);const auto result=op==14?cameraWorldToScreen(t,c,point):cameraScreenToWorld(t,c,point);
            lua_createtable(L,0,3);lua_pushnumber(L,result.x);lua_setfield(L,-2,"x");lua_pushnumber(L,result.y);lua_setfield(L,-2,"y");lua_pushnumber(L,result.z);lua_setfield(L,-2,"z");return 1;
        }
        }
        if(!validCamera(proposed))return luaL_error(L,"Invalid camera FOV, clip planes or normalized viewport");
        c=proposed;return 0;
    }catch(const std::exception& e){std::snprintf(error,sizeof(error),"%s",e.what());}
    return luaL_error(L,"%s",error);
}
