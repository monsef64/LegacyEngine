// Object-oriented methods share generation-safe GameObject/component handles.
int LuaScene::componentTypeIndex(lua_State* L) {
    const auto type=*static_cast<int*>(luaL_checkudata(L,1,"ComponentType"));
    const char* key=luaL_checkstring(L,2);
    if(type==6 && (!std::strcmp(key,"new") || !std::strcmp(key,"getActive"))) {
        lua_pushvalue(L,lua_upvalueindex(1));lua_pushinteger(L,!std::strcmp(key,"new")?0:2);
        lua_pushcclosure(L,cameraApi,2);return 1;
    }
    lua_pushnil(L);return 1;
}
int LuaScene::objectApi(lua_State* L) {
    const auto h=handle(L,1);
    const int op=static_cast<int>(lua_tointeger(L,lua_upvalueindex(2)))-200;
    if(op==8){h.world->add(h.entity,ObjectName{objectName(L,2)});return 0;}
    if(op==9){auto* name=h.world->tryGet<ObjectName>(h.entity);lua_pushstring(L,name?name->value.c_str():"");return 1;}
    if(op==10){
        bool enabled=lua_isnone(L,2)||lua_toboolean(L,2);
        if(!lua_isnone(L,2))luaL_checktype(L,2,LUA_TBOOLEAN);
        setObjectActive(*h.world,h.entity,enabled);
        if(auto* camera=h.world->tryGet<Camera>(h.entity)) { if(enabled) activateCamera(*h.world,h.entity);else camera->active=false; }
        return 0;
    }
    if(op==11){lua_pushboolean(L,isActive(*h.world,h.entity));return 1;}
    auto* transform=h.world->tryGet<Transform>(h.entity);
    if(!transform)return luaL_error(L,"GameObject has no Transform");
    switch(op) {
    case 0:transform->position=vectorArgument(L,2);break;
    case 1:pushVector(L,transform->position);return 1;
    case 2:transform->rotation=rotationArgument(L,2);
        if(auto* camera=h.world->tryGet<Camera>(h.entity)){camera->tracking=false;camera->following=false;}break;
    case 3: {
        auto q=transform->rotation;pushVector(L,{q.x,q.y,q.z});lua_pushnumber(L,q.w);lua_setfield(L,-2,"w");return 1;
    }
    case 4:transform->scale=vectorArgument(L,2);break;
    case 5:pushVector(L,transform->scale);return 1;
    case 6:transform->position=transform->position+vectorArgument(L,2);break;
    case 7: {
        Vec3 target;
        if(luaL_testudata(L,2,"GameObject")) {
            const auto other=handle(L,2);auto* pose=other.world->tryGet<Transform>(other.entity);
            if(!pose)return luaL_error(L,"Target has no Transform");target=pose->position;
        } else target=vectorArgument(L,2);
        aimCamera(*transform,target);
        if(auto* camera=h.world->tryGet<Camera>(h.entity)){camera->tracking=false;camera->following=false;}
        break;
    }
    }
    return 0;
}
