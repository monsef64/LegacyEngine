#pragma once
#include "assets/assets.hpp"
#include <map>
class MaterialTextureCache {
    struct Entry{unsigned texture=0;std::size_t bytes=0;std::uint64_t used=0;std::filesystem::file_time_type stamp{};std::uintmax_t size=0;std::chrono::steady_clock::time_point checked{};};
    std::map<std::string,std::pair<std::chrono::steady_clock::time_point,std::string>> failures_;
    std::map<std::string,Entry> entries_;std::size_t bytes_=0;std::uint64_t frame_=0;
public:
    ~MaterialTextureCache(){clear();}
    void clear();
    void beginFrame(){++frame_;}
    unsigned get(const AssetManager& assets,const std::string& path,std::string& error);
};
