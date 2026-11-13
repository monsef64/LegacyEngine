#include "assets/texture_preview.hpp"
#include "project/json_io.hpp"
#include <cassert>
#include <fstream>
int main(){
    const auto temp=std::filesystem::temp_directory_path()/("legacy-thumbnails-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AssetManager assets;assets.root=temp/"Assets";std::filesystem::create_directories(assets.root);
    auto image=[&](unsigned char red){
        std::ofstream out(assets.root/"color.bmp",std::ios::binary);
        auto word=[&](unsigned v,int bytes){for(int i=0;i<bytes;++i)out.put(char(v>>(8*i)));};
        out<<"BM";word(54+24,4);word(0,4);word(54,4);word(40,4);word(4,4);word(2,4);word(1,2);word(24,2);word(0,4);word(24,4);for(int i=0;i<4;++i)word(0,4);
        for(int i=0;i<8;++i){out.put(0);out.put(0);out.put(char(red));}
    };
    image(200);bool hit=false;auto a=loadTextureThumbnail(assets,"color.bmp",&hit);assert(!hit);
    auto cache=thumbnailCachePath(assets,"color.bmp");assert(cache.parent_path()==temp/".cache"/"thumbnails");assert(std::filesystem::exists(cache));
    assert(a.pixels[((TextureThumbnail::size/4)*TextureThumbnail::size)*4]==200 && a.pixels[((TextureThumbnail::size/4)*TextureThumbnail::size)*4+3]==255 && a.pixels[3]==0);
    auto b=loadTextureThumbnail(assets,"color.bmp",&hit);assert(hit && a.pixels==b.pixels);
    image(20);std::filesystem::last_write_time(assets.root/"color.bmp",std::filesystem::last_write_time(assets.root/"color.bmp")+std::chrono::seconds(2));
    b=loadTextureThumbnail(assets,"color.bmp",&hit);assert(!hit && b.pixels[((TextureThumbnail::size/4)*TextureThumbnail::size)*4]==20);
    {std::ofstream corrupt(cache);corrupt<<"broken cache";}
    b=loadTextureThumbnail(assets,"color.bmp",&hit);assert(!hit && b.pixels[((TextureThumbnail::size/4)*TextureThumbnail::size)*4]==20);
    {std::ofstream bad(assets.root/"invalid.png");bad<<"not PNG";}
    bool rejected=false;try{loadTextureThumbnail(assets,"invalid.png");}catch(...){rejected=true;}assert(rejected);
    rejected=false;try{loadTextureThumbnail(assets,"../escape.png");}catch(...){rejected=true;}assert(rejected);
    {std::ofstream hdr(assets.root/"sky.hdr",std::ios::binary);hdr<<"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";for(int i=0;i<4;++i){hdr.put(char(128));hdr.put(char(128));hdr.put(char(128));hdr.put(char(129));}}
    assert(textureAsset("sky.hdr"));auto hdr=loadTextureThumbnail(assets,"sky.hdr");assert(hdr.pixels[(TextureThumbnail::size/2*TextureThumbnail::size+TextureThumbnail::size/2)*4]>0);
    // simdjson conversion preserves types, Unicode, malformed/deep rejection.
    auto json=parseJson("{\"i\":-3,\"u\":18446744073709551615,\"s\":\"\\u00e9\",\"b\":true,\"a\":[null,1.25]}");
    assert(json["i"]==-3 && json["u"].get<std::uint64_t>()==UINT64_MAX && json["s"]=="é" && json["b"]==true && json["a"][0].is_null());
    for(const auto& text:{std::string("{bad}"),std::string(70,'[')+"0"+std::string(70,']')}){rejected=false;try{parseJson(text);}catch(...){rejected=true;}assert(rejected);}
    std::filesystem::remove_all(temp);
}
