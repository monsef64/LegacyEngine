#include "assets/texture_preview.hpp"
#include <SDL3/SDL.h>
#include <glad/gl.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MAX_DIMENSIONS 16384
#include <stb_image.h>
#include <fstream>
#include <sstream>
#include <memory>

bool textureAsset(const std::string& path){const auto e=AssetManager::extension(path);return e==".png" || e==".jpg" || e==".jpeg" || e==".bmp" || e==".tga";}
std::filesystem::path thumbnailCachePath(const AssetManager& assets,const std::string& path){
    assets.resolve(path);std::uint64_t hash=14695981039346656037ull;
    for(unsigned char c:std::filesystem::path(path).generic_string()){hash^=c;hash*=1099511628211ull;}
    std::ostringstream name;name<<std::hex<<hash<<".rgba";
    const auto project=assets.root.filename()=="Assets"?assets.root.parent_path():assets.root;
    auto cache=project/".cache",folder=cache/"thumbnails";
    if(std::filesystem::is_symlink(cache) || std::filesystem::is_symlink(folder))throw std::runtime_error("Thumbnail cache may not be a symlink");
    auto file=folder/name.str();if(std::filesystem::is_symlink(file))throw std::runtime_error("Thumbnail entry may not be a symlink");return file;
}
TextureThumbnail loadTextureThumbnail(const AssetManager& assets,const std::string& path,bool* cacheHit,bool allowDecode){
    if(cacheHit)*cacheHit=false;if(!textureAsset(path))throw std::runtime_error("Unsupported thumbnail format");
    const auto source=assets.resolve(path);const auto bytes=std::filesystem::file_size(source);
    if(bytes>32*1024*1024)throw std::runtime_error("Texture exceeds thumbnail input limit");
    const auto stamp=std::filesystem::last_write_time(source).time_since_epoch().count();
    auto cache=thumbnailCachePath(assets,path);TextureThumbnail result;
    {std::ifstream in(cache,std::ios::binary);std::string header;
        if(in && std::filesystem::file_size(cache)<17000 && std::getline(in,header)){
            std::istringstream metadata(header);std::string magic;std::uintmax_t savedBytes=0;std::int64_t savedStamp=0;
            metadata>>magic>>savedBytes>>savedStamp;
            if(metadata && magic=="LegacyThumbnail1" && savedBytes==bytes && savedStamp==stamp){
                in.read(reinterpret_cast<char*>(result.pixels.data()),result.pixels.size());
                if(in.gcount()==static_cast<std::streamsize>(result.pixels.size()) && in.peek()==std::char_traits<char>::eof()){if(cacheHit)*cacheHit=true;return result;}
            }
        }
    }
    if(!allowDecode)throw std::runtime_error("Thumbnail is not cached");
    int width=0,height=0,channels=0;
    // Bounded encoded read also handles Unicode filenames on Windows.
    std::ifstream file(source,std::ios::binary);if(!file)throw std::runtime_error("Cannot read texture");
    std::vector<unsigned char> encoded(static_cast<std::size_t>(bytes));
    file.read(reinterpret_cast<char*>(encoded.data()),static_cast<std::streamsize>(bytes));
    if(!file || file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Texture changed during preview read");
    if(encoded.size()!=bytes || !stbi_info_from_memory(encoded.data(),int(encoded.size()),&width,&height,&channels) || width<=0 || height<=0 || std::uint64_t(width)*height>16*1024*1024)throw std::runtime_error("Invalid or oversized texture");
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(encoded.data(),int(encoded.size()),&width,&height,&channels,4),stbi_image_free);
    if(!pixels)throw std::runtime_error("Cannot decode texture preview");
    const float factor=std::min(float(TextureThumbnail::size)/width,float(TextureThumbnail::size)/height);
    const int outW=std::max(1,int(width*factor)),outH=std::max(1,int(height*factor));
    const int left=(TextureThumbnail::size-outW)/2,top=(TextureThumbnail::size-outH)/2;
    for(int y=0;y<outH;++y)for(int x=0;x<outW;++x){
        // Area average avoids aliasing high-frequency texture patterns.
        const int x0=x*width/outW,x1=std::max(x0+1,(x+1)*width/outW),y0=y*height/outH,y1=std::max(y0+1,(y+1)*height/outH);
        std::uint64_t sum[4]{};for(int sy=y0;sy<y1;++sy)for(int sx=x0;sx<x1;++sx)for(int c=0;c<4;++c)sum[c]+=pixels.get()[(std::size_t(sy)*width+sx)*4+c];
        const auto count=(x1-x0)*(y1-y0);for(int c=0;c<4;++c)result.pixels[((y+top)*TextureThumbnail::size+x+left)*4+c]=static_cast<unsigned char>(sum[c]/count);
    }
    // Cache writes are best effort: a read-only project can still display previews.
    try{
        std::filesystem::create_directories(cache.parent_path());auto temp=cache;temp+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        try{std::ofstream out(temp,std::ios::binary);out<<"LegacyThumbnail1 "<<bytes<<' '<<stamp<<'\n';out.write(reinterpret_cast<const char*>(result.pixels.data()),result.pixels.size());out.close();if(!out)throw std::runtime_error("Cannot write thumbnail cache");
            std::error_code ec;std::filesystem::rename(temp,cache,ec);if(ec){std::filesystem::remove(cache,ec);ec.clear();std::filesystem::rename(temp,cache,ec);if(ec)std::filesystem::remove(temp,ec);}
        }catch(...){std::error_code ec;std::filesystem::remove(temp,ec);}
    }catch(...){}
    return result;
}
void TexturePreviewCache::clear(){
    if(SDL_GL_GetCurrentContext())for(auto& pair:entries_)if(pair.second.texture)glDeleteTextures(1,&pair.second.texture);
    entries_.clear();root_.clear();
}
void TexturePreviewCache::beginFrame(const AssetManager& assets){if(root_!=assets.root){clear();root_=assets.root;}++frame_;budget_=4;decodeBudget_=1;}
unsigned TexturePreviewCache::texture(const AssetManager& assets,const std::string& path){
    if(!textureAsset(path) || !SDL_GL_GetCurrentContext())return 0;
    auto found=entries_.find(path);const auto now=std::chrono::steady_clock::now();
    if(found!=entries_.end()){
        auto& e=found->second;e.touched=frame_;
        if(now-e.checked<std::chrono::seconds(1))return e.texture;
        e.checked=now;
        try{const auto file=assets.resolve(path);if(e.modified==std::filesystem::last_write_time(file) && e.bytes==std::filesystem::file_size(file))return e.texture;}catch(...){return 0;}
        if(e.texture)glDeleteTextures(1,&e.texture);entries_.erase(found);
    }
    if(budget_<=0)return 0;--budget_;
    if(entries_.size()>=256){auto oldest=std::min_element(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.second.touched<b.second.touched;});if(oldest->second.texture)glDeleteTextures(1,&oldest->second.texture);entries_.erase(oldest);}
    auto& entry=entries_[path];entry.touched=frame_;entry.checked=now;
    try{
        const auto file=assets.resolve(path);entry.modified=std::filesystem::last_write_time(file);entry.bytes=std::filesystem::file_size(file);
        bool cacheHit=false;auto image=loadTextureThumbnail(assets,path,&cacheHit,decodeBudget_>0);if(!cacheHit)--decodeBudget_;glGenTextures(1,&entry.texture);glBindTexture(GL_TEXTURE_2D,entry.texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,TextureThumbnail::size,TextureThumbnail::size,0,GL_RGBA,GL_UNSIGNED_BYTE,image.pixels.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }catch(...){if(decodeBudget_<=0){entries_.erase(path);return 0;}--decodeBudget_;entry.failed=true;}
    return entry.texture;
}
