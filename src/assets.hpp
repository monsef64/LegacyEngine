#pragma once
#include "components.hpp"
#include "ecs.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>
#include <cctype>
#include <chrono>

class AssetManager {
public:
    std::filesystem::path root; // Must be supplied by the owning project.
    std::string error;
    static std::string extension(const std::filesystem::path& p) {
        auto e=p.extension().string();for(char& c:e)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return e;
    }
    static bool script(const std::filesystem::path& p) {return extension(p)==".lua";}
    std::filesystem::path resolve(const std::string& relative) const {
        if(root.empty())throw std::runtime_error("No project asset root is configured");
        const std::filesystem::path p(relative);
        if(relative.find('\0')!=std::string::npos)throw std::runtime_error("Invalid asset path");
        if(p.is_absolute())throw std::runtime_error("Asset path must be relative");
        for(const auto& part:p)if(part=="..")throw std::runtime_error("Asset path escapes Assets");
        auto candidate=std::filesystem::weakly_canonical(root/p);
        auto canonicalRoot=std::filesystem::weakly_canonical(root);
        auto r=candidate.lexically_relative(canonicalRoot);
        if(r.empty() || *r.begin()=="..")throw std::runtime_error("Asset path escapes Assets");
        return candidate;
    }
    std::vector<std::string> files() const {
        std::vector<std::string> result;
        if(root.empty() || !std::filesystem::exists(root))return result;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root)) {
            if(result.size()>=10000)break;
            if(entry.is_regular_file() && !entry.is_symlink()) {
                auto relative=entry.path().lexically_relative(root).generic_string();
                resolve(relative);result.push_back(relative);
            }
        }
        std::sort(result.begin(),result.end());return result;
    }
    std::string import(const std::filesystem::path& source) {
        if(root.empty())throw std::runtime_error("No project asset root is configured");
        if(!std::filesystem::is_regular_file(source))throw std::runtime_error("Drop individual files, not directories");
        if(std::filesystem::file_size(source)>256*1024*1024)throw std::runtime_error("Asset exceeds 256 MiB import limit");
        std::filesystem::create_directories(root);
        if(source.parent_path()==root)return source.filename().generic_string();
        auto name=source.filename();int n=2;
        while(std::filesystem::exists(root/name)) {
            if(std::filesystem::equivalent(source,root/name))return name.generic_string();
            name=source.stem().string()+" ("+std::to_string(n++)+")"+source.extension().string();
        }
        std::filesystem::copy_file(source,root/name);return name.generic_string();
    }
    std::string readScript(const std::string& path) const {
        auto file=resolve(path);
        if(!script(file) || std::filesystem::file_size(file)>64*1024)throw std::runtime_error("Expected a Lua asset, at most 64 KiB");
        std::ifstream in(file,std::ios::binary);if(!in)throw std::runtime_error("Cannot read Lua asset");
        return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
    }
    void writeScript(const std::string& path,std::string_view source) {
        if(!script(path) || source.size()>64*1024)throw std::runtime_error("Invalid Lua asset");
        auto destination=resolve(path);std::filesystem::create_directories(destination.parent_path());
        auto temp=destination;temp+=".writing-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(source.data(),source.size());out.close();
        if(!out)throw std::runtime_error("Cannot save Lua asset");
        std::filesystem::rename(temp,destination);
    }
    // Save As copies the asset tree without changing existing project assets.
    // Conflicting content is rejected rather than silently breaking references.
    void copyTo(const std::filesystem::path& destination) const {
        if(root.empty())throw std::runtime_error("No project asset root is configured");
        std::filesystem::create_directories(destination);
        if(std::filesystem::weakly_canonical(root)==std::filesystem::weakly_canonical(destination))return;
        for(const auto& path:files()) {
            const auto from=resolve(path),to=destination/path;
            std::filesystem::create_directories(to.parent_path());
            if(std::filesystem::exists(to)) {
                std::ifstream a(from,std::ios::binary),b(to,std::ios::binary);
                if(std::string(std::istreambuf_iterator<char>(a),{})!=std::string(std::istreambuf_iterator<char>(b),{}))
                    throw std::runtime_error("Save As asset conflict: "+path);
            } else std::filesystem::copy_file(from,to);
        }
    }
};
inline bool attachScriptAsset(World& world,Entity entity,const std::string& path) {
    if(!world.alive(entity) || !AssetManager::script(path))return false;
    if(std::filesystem::path(path).is_absolute())return false;
    for(const auto& part:std::filesystem::path(path))if(part=="..")return false;
    world.add(entity,LuaScript{path,true});return true;
}
