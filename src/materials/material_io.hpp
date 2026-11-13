#pragma once
#include "materials/material.hpp"
#include "assets/assets.hpp"
#include "project/json_io.hpp"
#include <map>
inline bool materialAsset(const std::string& path){return materialPathValid(path) && AssetManager::extension(path)==".mat";}
inline Json materialJson(const MaterialProperties& p){
    if(!validMaterial(p))throw std::runtime_error("Invalid material properties");
    return {{"format","LegacyEngineMaterial"},{"version",1},{"shader",p.shader==1?"Bumped Diffuse":"Diffuse"},{"color",{p.color.x,p.color.y,p.color.z}},
        {"texture",p.texture},{"normalMap",p.normalMap},{"normalStrength",p.normalStrength},{"tiling",{p.tilingX,p.tilingY}},{"specular",p.specular}};
}
inline MaterialProperties parseMaterial(const Json& json){
    if(json.at("format")!="LegacyEngineMaterial" || json.at("version")!=1)throw std::runtime_error("Invalid material file");
    MaterialProperties p;const auto shader=json.at("shader").get<std::string>();if(shader!="Diffuse" && shader!="Bumped Diffuse")throw std::runtime_error("Unknown material shader");p.shader=shader=="Bumped Diffuse";
    const auto& color=json.at("color");const auto& tiling=json.at("tiling");if(!color.is_array() || color.size()!=3 || !tiling.is_array() || tiling.size()!=2)throw std::runtime_error("Invalid material vectors");
    p.color={color[0].get<float>(),color[1].get<float>(),color[2].get<float>()};p.tilingX=tiling[0].get<float>();p.tilingY=tiling[1].get<float>();
    p.texture=json.at("texture").get<std::string>();p.normalMap=json.at("normalMap").get<std::string>();p.normalStrength=json.at("normalStrength").get<float>();p.specular=json.at("specular").get<float>();
    if(!validMaterial(p))throw std::runtime_error("Invalid material properties");return p;
}
inline MaterialProperties loadMaterial(const AssetManager& assets,const std::string& path){if(!materialAsset(path))throw std::runtime_error("Expected a project .mat asset");return parseMaterial(readJsonFile(assets.resolve(path),64*1024));}
inline void saveMaterial(const AssetManager& assets,const std::string& path,const MaterialProperties& p){if(!materialAsset(path))throw std::runtime_error("Expected a project .mat asset");writeJsonFile(assets.resolve(path),materialJson(p));}
inline std::string createMaterial(const AssetManager& assets,const std::string& folder=""){
    auto relative=(std::filesystem::path(folder)/"NewMaterial.mat").generic_string();int suffix=2;
    while(std::filesystem::exists(assets.resolve(relative)))relative=(std::filesystem::path(folder)/("NewMaterial ("+std::to_string(suffix++)+").mat")).generic_string();
    saveMaterial(assets,relative,MaterialProperties{});return relative;
}
class MaterialAssetCache {
    struct Entry{MaterialProperties properties;std::filesystem::file_time_type modified{};std::uintmax_t bytes=0;std::chrono::steady_clock::time_point checked{};bool valid=false;};
    std::map<std::string,Entry> entries_;
public:
    void clear(){entries_.clear();}
    MaterialProperties get(const AssetManager& assets,const std::string& path){
        if(entries_.size()>=1024 && !entries_.count(path))throw std::runtime_error("Material cache exceeds 1024 assets");
        auto& entry=entries_[path];auto now=std::chrono::steady_clock::now();
        if(entry.valid && now-entry.checked<std::chrono::milliseconds(250))return entry.properties;
        const auto file=assets.resolve(path);const auto stamp=std::filesystem::last_write_time(file);const auto bytes=std::filesystem::file_size(file);
        entry.checked=now;if(!entry.valid || entry.modified!=stamp || entry.bytes!=bytes){entry.properties=loadMaterial(assets,path);entry.modified=stamp;entry.bytes=bytes;entry.valid=true;}
        return entry.properties;
    }
};
