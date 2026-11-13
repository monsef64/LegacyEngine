#pragma once
#include "project/json_io.hpp"
#include "materials/material_io.hpp"
#include <sstream>
#include <iomanip>
#include <limits>
namespace ProjectJson {
// Shared typed schema bridges the validated legacy loader and the structured JSON format.
struct Field{const char* name;char type;int count=1;};
inline Json read(std::istream& in,std::initializer_list<Field> fields){
    Json object=Json::object();
    for(auto field:fields){Json values=Json::array();for(int i=0;i<field.count;++i){
        if(field.type=='j'){std::string value;in>>std::quoted(value);values.push_back(parseJson(value));}
        else if(field.type=='s'){std::string value;in>>std::quoted(value);values.push_back(value);}
        else if(field.type=='b'){bool value=false;in>>value;values.push_back(value);}
        else if(field.type=='i'){int value=0;in>>value;values.push_back(value);}
        else{double value=0;in>>value;values.push_back(value);}
        if(!in)throw std::runtime_error("Invalid scene data");
    }object[field.name]=field.count==1?values[0]:values;}return object;
}
inline void write(std::ostream& out,const Json& object,std::initializer_list<Field> fields){
    for(auto field:fields){const auto& item=object.at(field.name);if(field.count>1 && (!item.is_array() || item.size()!=static_cast<std::size_t>(field.count)))throw std::runtime_error("Invalid vector in scene JSON");
        for(int i=0;i<field.count;++i){const auto& value=field.count==1?item:item.at(i);
            if(field.type=='j')out<<std::quoted(value.dump());
            else if(field.type=='s')out<<std::quoted(value.get<std::string>());
            else if(field.type=='b')out<<value.get<bool>();
            else if(field.type=='i'){if(!value.is_number_integer())throw std::runtime_error("Expected integer in scene JSON");auto integer=value.get<std::int64_t>();if(integer<std::numeric_limits<int>::min() || integer>std::numeric_limits<int>::max())throw std::runtime_error("Integer exceeds project range");out<<integer;}
            else{if(!value.is_number())throw std::runtime_error("Expected number in scene JSON");out<<value.get<double>();}out<<' ';
        }
    }
}
inline const std::vector<std::pair<const char*,std::vector<Field>>> components{
 {"ObjectName",{{"value",'s'}}},
 {"Transform",{{"position",'f',3},{"rotation",'f',4},{"scale",'f',3}}},
 {"MeshRenderer",{{"mesh",'i'},{"color",'f',3},{"checkerboard",'b'},{"specular",'f'},{"material",'s'},{"materialOverrides",'i'},{"materialValues",'j'}}},
 {"Collider",{{"size",'f',3},{"center",'f',3},{"friction",'f'},{"bounciness",'f'},{"shape",'i'},{"radius",'f'},{"height",'f'}}},
 {"RigidBody",{{"mass",'f'},{"useGravity",'b'},{"linearVelocity",'f',3},{"angularVelocity",'f',3}}},
 {"LuaScript",{}},
 {"Camera",{{"active",'b'},{"orthographic",'b'},{"fieldOfView",'f'},{"nearClip",'f'},{"farClip",'f'},{"orthographicSize",'f'},{"viewport",'f',4},{"targetEntity",'i'},{"target",'f',3},{"followOffset",'f',3},{"tracking",'b'},{"following",'b'}}},
 {"AudioSource",{{"clip",'s'},{"volume",'f'},{"pitch",'f'},{"pan",'f'},{"maxDistance",'f'},{"looping",'b'},{"muted",'b'},{"playOnStart",'b'},{"spatialBlend",'f'}}},
 {"AudioListener",{{"enabled",'b'}}},
 {"DirectionalLight",{{"color",'f',3},{"intensity",'f'},{"enabled",'b'},{"shadows",'b'},{"range",'f'},{"innerAngle",'f'},{"outerAngle",'f'},{"shadowDistance",'f'},{"shadowBias",'f'},{"shadowQuality",'i'}}},
 {"PointLight",{{"color",'f',3},{"intensity",'f'},{"enabled",'b'},{"shadows",'b'},{"range",'f'},{"innerAngle",'f'},{"outerAngle",'f'},{"shadowDistance",'f'},{"shadowBias",'f'},{"shadowQuality",'i'}}},
 {"SpotLight",{{"color",'f',3},{"intensity",'f'},{"enabled",'b'},{"shadows",'b'},{"range",'f'},{"innerAngle",'f'},{"outerAngle",'f'},{"shadowDistance",'f'},{"shadowBias",'f'},{"shadowQuality",'i'}}},
 {"Skybox",{{"type",'i'},{"color",'f',3},{"enabled",'b'},{"right",'s'},{"left",'s'},{"top",'s'},{"bottom",'s'},{"front",'s'},{"back",'s'},{"hdr",'s'}}}
};
// Vector schemas are iterated through a one-field adapter to keep both directions identical.
inline Json readSchema(std::istream& in,const std::vector<Field>& fields){Json result=Json::object();for(auto f:fields){auto part=read(in,{f});result.update(part);}return result;}
inline void writeSchema(std::ostream& out,const Json& object,const std::vector<Field>& fields){for(auto f:fields){if(std::string(f.name)=="shadowQuality" && !object.contains(f.name))out<<3<<' ';else if(!object.contains(f.name) && std::string(f.name)=="material")out<<std::quoted(std::string{})<<' ';
        else if(!object.contains(f.name) && std::string(f.name)=="materialOverrides")out<<0<<' ';
        else if(!object.contains(f.name) && std::string(f.name)=="materialValues")out<<std::quoted(materialJson(MaterialProperties{}).dump())<<' ';
        else write(out,object,{f});}}
inline Json readFields(std::istream& in){int count=0;in>>count;if(count<0 || count>64)throw std::runtime_error("Too many exported fields");Json result=Json::array();for(int i=0;i<count;++i)result.push_back(read(in,{{"name",'s'},{"type",'s'},{"text",'s'},{"number",'f'},{"boolean",'b'},{"entity",'i'}}));return result;}
inline void writeFields(std::ostream& out,const Json& fields){if(!fields.is_array() || fields.size()>64)throw std::runtime_error("Too many exported fields");out<<fields.size()<<' ';for(const auto& field:fields)write(out,field,{{"name",'s'},{"type",'s'},{"text",'s'},{"number",'f'},{"boolean",'b'},{"entity",'i'}});}
inline Json fromLegacy(const std::string& text){
    std::istringstream in(text);std::string magic;int version=0;in>>magic>>version;
    Json doc=read(in,{{"script",'s'},{"assets",'s'}});doc["format"]="LegacyEngine";doc["version"]=9;
    doc["editorCamera"]=read(in,{{"target",'f',3},{"yaw",'f'},{"pitch",'f'},{"distance",'f'},{"fieldOfView",'f'}});
    int count=0;in>>count;doc["objects"]=Json::array();
    for(int i=0;i<count;++i){unsigned mask=0;int parent=-1;in>>mask>>parent;Json object={{"parent",parent},{"components",Json::object()}};
        for(unsigned type=0;type<components.size();++type)if(mask&(1u<<type)){
            Json value;
            if(type==5){value=Json::array();value.push_back(read(in,{{"path",'s'},{"enabled",'b'}}));int extra=0;in>>extra;for(int j=0;j<extra;++j)value.push_back(read(in,{{"path",'s'},{"enabled",'b'}}));for(auto& script:value)script["fields"]=readFields(in);}
            else value=readSchema(in,components[type].second);
            object["components"][components[type].first]=value;
        }doc["objects"].push_back(object);
    }return doc;
}
inline std::string toLegacy(const Json& doc){
    if(doc.at("format")!="LegacyEngine" || doc.at("version")!=9 || !doc.at("objects").is_array() || doc.at("objects").size()>10000)throw std::runtime_error("Invalid JSON project format");
    std::ostringstream out;out<<std::setprecision(17)<<"LegacyEngineProject 12\n";
    write(out,doc,{{"script",'s'},{"assets",'s'}});write(out,doc.at("editorCamera"),{{"target",'f',3},{"yaw",'f'},{"pitch",'f'},{"distance",'f'},{"fieldOfView",'f'}});
    out<<doc.at("objects").size()<<' ';
    for(const auto& object:doc.at("objects")){
        const auto& data=object.at("components");if(!data.is_object())throw std::runtime_error("Invalid components");unsigned mask=0;for(unsigned type=0;type<components.size();++type)if(data.contains(components[type].first))mask|=1u<<type;
        out<<mask<<' ';write(out,object,{{"parent",'i'}});
        for(unsigned type=0;type<components.size();++type)if(mask&(1u<<type)){
            const auto& value=data.at(components[type].first);
            if(type==5){if(!value.is_array() || value.empty() || value.size()>64)throw std::runtime_error("Invalid scripts");write(out,value[0],{{"path",'s'},{"enabled",'b'}});out<<value.size()-1<<' ';for(std::size_t j=1;j<value.size();++j)write(out,value[j],{{"path",'s'},{"enabled",'b'}});for(const auto& script:value)writeFields(out,script.at("fields"));}
            else writeSchema(out,value,components[type].second);
        }
    }return out.str();
}
}
