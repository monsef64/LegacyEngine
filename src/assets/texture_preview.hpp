#pragma once
#include "assets/assets.hpp"
#include <array>
#include <map>
#include <chrono>

struct TextureThumbnail {
    static constexpr int size=64;
    std::array<unsigned char,size*size*4> pixels{};
};
bool textureAsset(const std::string& path);
// Stable per-asset disk entry; metadata in the header invalidates stale content.
std::filesystem::path thumbnailCachePath(const AssetManager& assets,const std::string& path);
TextureThumbnail loadTextureThumbnail(const AssetManager& assets,const std::string& path,bool* cacheHit=nullptr,bool allowDecode=true);
class TexturePreviewCache {
    struct Entry {unsigned texture=0;std::uint64_t touched=0;std::filesystem::file_time_type modified{};std::uintmax_t bytes=0;bool failed=false;std::chrono::steady_clock::time_point checked{};};
    std::map<std::string,Entry> entries_;
    std::filesystem::path root_;
    std::uint64_t frame_=0;
    int budget_=0,decodeBudget_=0;
public:
    ~TexturePreviewCache(){clear();}
    void beginFrame(const AssetManager& assets);
    unsigned texture(const AssetManager& assets,const std::string& path);
    void clear();
};
