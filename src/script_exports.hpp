#pragma once
#include "components.hpp"
#include "legacy_script.hpp"
#include <sstream>
#include <iomanip>
#include <cctype>
#include <filesystem>
inline bool exportReferenceType(const std::string& type){
    return type=="GameObject" || type=="Transform" || type=="RigidBody" || type=="Collider" || type=="MeshRenderer" || type=="ObjectName" || type=="LuaScript" || type=="Camera" || type=="AudioSource" || type=="AudioListener";
}
inline bool exportAssetType(const std::string& type){return type=="Asset" || type=="AudioClip";}
inline bool exportType(const std::string& type){return exportReferenceType(type)||exportAssetType(type)||type=="number"||type=="boolean"||type=="string";}
inline bool exportAssetPath(const std::string& path){
    if(path.size()>4096 || path.find('\0')!=std::string::npos || std::filesystem::path(path).is_absolute())return false;
    for(const auto& part:std::filesystem::path(path))if(part=="..")return false;
    return true;
}
inline bool exportName(const std::string& name){
    if(name.empty() || name.size()>128 || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0]=='_'))return false;
    for(unsigned char c:name)if(!std::isalnum(c)&&c!='_')return false;
    return name!="self" && name!="onStart" && name!="onUpdate" && name!="onFixedUpdate" && name!="onDestroy";
}
inline void syncScriptExports(std::string_view source,std::vector<ScriptField>& fields){
    std::vector<ScriptField> next;
    for(const auto& declaration:exportDeclarations(source)){
        if(next.size()>=64)break;
        std::istringstream in(std::string(source.substr(declaration.body,declaration.end-declaration.body)));ScriptField field;in>>field.name>>field.type;
        if(!exportName(field.name) || !exportType(field.type))continue;
        bool duplicate=false;for(const auto& f:next)if(f.name==field.name)duplicate=true;if(duplicate)continue;
        if(field.type=="number"){in>>field.number;if(!std::isfinite(field.number))field.number=0;field.number=std::clamp(field.number,-1000000.0f,1000000.0f);}
        else if(field.type=="boolean"){std::string value;in>>value;field.boolean=value=="true";}
        else if(field.type=="string" || exportAssetType(field.type))in>>std::quoted(field.text);
        if(field.text.size()>4096)field.text.resize(4096);
        if(exportAssetType(field.type) && !exportAssetPath(field.text))field.text.clear();
        for(const auto& old:fields)if(old.name==field.name && old.type==field.type){field=old;break;}
        next.push_back(field);
    }
    fields=std::move(next);
}
inline bool exportHasComponent(World& world,Entity e,const std::string& type){
    if(!world.alive(e))return false;
    if(type=="GameObject")return true;
#define EXPORT_COMPONENT(T) if(type==#T)return world.has<T>(e)
    EXPORT_COMPONENT(Transform);EXPORT_COMPONENT(RigidBody);EXPORT_COMPONENT(Collider);EXPORT_COMPONENT(MeshRenderer);EXPORT_COMPONENT(ObjectName);EXPORT_COMPONENT(LuaScript);EXPORT_COMPONENT(Camera);EXPORT_COMPONENT(AudioSource);EXPORT_COMPONENT(AudioListener);
#undef EXPORT_COMPONENT
    return false;
}
