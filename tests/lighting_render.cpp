// Explicit graphical integration test, run under Xvfb in CI (not headless CTest).
#include "rendering/renderer.hpp"
#include "editor/rotation_editor.hpp"
#include "scripting/lua_console.hpp"
#include "input/input.hpp"
#include "assets/texture_preview.hpp"
#include <fstream>
#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include <set>
#include <type_traits>

int main(){
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window=SDL_CreateWindow("Lighting regression",192,192,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(window);
    auto context=SDL_GL_CreateContext(window);assert(context);
    Renderer renderer;assert(renderer.init());
    InputState input;input.window=window;LuaConsole console;
    assert(console.execute("assert(showCursor()); assert(isCursorVisible()); assert(hideCursor()); assert(not isCursorVisible()); assert(showCursor()); assert(lockCursor()); assert(isCursorLocked()); assert(not isCursorVisible()); assert(unlockCursor()); assert(not isCursorLocked())"));
    SDL_Event lost{};lost.type=SDL_EVENT_WINDOW_FOCUS_LOST;input.event(lost);assert(!input.mouseLocked && input.mouseVisible);

    const auto cube=renderer.createMesh(makeBox({2,2,2})),plane=renderer.createMesh(makePlane(20,20));
    World world;auto floor=world.create();world.add(floor,Transform{});world.add(floor,MeshRenderer{plane,{.7f,.7f,.7f}});
    auto box=world.create();world.add(box,Transform{{0,1,0}});world.add(box,MeshRenderer{cube,{.7f,.3f,.2f}});
    const Vec3 eye{7,6,7};const auto vp=multiply(perspective(kPi/3,1,.1f,100),lookAt(eye,{0,0,0},{0,1,0}));
    auto lamp=world.create();Transform t;t.position={0,6,3};t.rotation=rotationFromEuler({-65,0,0});world.add(lamp,t);
    auto read=[&](){
        renderer.draw(world,vp,192,192,eye,0,0,false);glFinish();
        assert(glGetError()==GL_NO_ERROR);
        std::vector<unsigned char> pixels(192*192*3);glReadPixels(0,0,192,192,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());return pixels;
    };
    auto difference=[](const auto& a,const auto& b){int sum=0;for(std::size_t i=0;i<a.size();++i)sum+=std::abs(int(a[i])-int(b[i]));return sum;};
    auto check=[&](auto tag){
        using T=decltype(tag);T light;light.intensity=4;light.range=25;light.shadowDistance=20;world.add(lamp,light);
        const auto shadowed=read();world.get<T>(lamp).shadows=false;const auto unshadowed=read();
        for(int quality=1;quality<=3;++quality){
            world.get<T>(lamp).shadowQuality=quality;world.get<T>(lamp).shadows=true;const auto filtered=read();assert(difference(filtered,unshadowed)>100);
            // A flat, uniformly lit gray floor had binary edges with nearest
            // Low sampling. Require multiple fractional coverage levels now.
            if constexpr(std::is_same_v<T,DirectionalLight>){
                std::set<int> coverage;
                for(std::size_t pixel=0;pixel<filtered.size();pixel+=3){
                    int value=filtered[pixel];
                    if(value==filtered[pixel+1] && value==filtered[pixel+2] && value>30 && value+5<unshadowed[pixel])coverage.insert(value);
                }
                std::printf("quality %d fractional shadow levels: %zu\n",quality,coverage.size());assert(coverage.size()>=6);
            }
        }
        world.get<T>(lamp).shadowQuality=0;assert(difference(read(),unshadowed)==0);world.get<T>(lamp).shadowQuality=3;world.get<T>(lamp).shadows=false;
        const int shadowDiff=difference(shadowed,unshadowed);std::printf("shadow difference: %d\n",shadowDiff);assert(shadowDiff>100);
        world.get<T>(lamp).shadows=true;world.get<Transform>(box).position.x=2;const auto moved=read();assert(difference(shadowed,moved)>100);
        world.get<T>(lamp).shadows=false;assert(difference(moved,read())>100);world.get<T>(lamp).shadows=true;
        world.get<Transform>(box).position.x=0;world.get<Transform>(lamp).rotation=rotationFromEuler({-40,25,0});world.get<Transform>(lamp).position.x=3;
        const auto movedLight=read();assert(difference(shadowed,movedLight)>100);
        world.get<T>(lamp).shadows=false;assert(difference(movedLight,read())>100);
        world.remove<T>(lamp);world.get<Transform>(lamp)=t;
    };
    check(DirectionalLight{});check(PointLight{});check(SpotLight{});
    // Exercise real GL thumbnail upload and persistent cache, not only CPU pixels.
    const auto temp=std::filesystem::temp_directory_path()/("legacy-preview-gl-"+std::to_string(SDL_GetTicksNS()));
    AssetManager assets;assets.root=temp/"Assets";std::filesystem::create_directories(assets.root);
    {std::ofstream image(assets.root/"red.tga",std::ios::binary);unsigned char header[18]{};header[2]=2;header[12]=2;header[14]=2;header[16]=24;image.write(reinterpret_cast<char*>(header),18);for(int i=0;i<4;++i){image.put(0);image.put(0);image.put(char(255));}}
    TexturePreviewCache previews;previews.beginFrame(assets);const auto texture=previews.texture(assets,"red.tga");assert(texture!=0);
    glBindTexture(GL_TEXTURE_2D,texture);TextureThumbnail pixels;glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.pixels.data());
    assert(pixels.pixels[0]==255 && pixels.pixels[3]==255 && glGetError()==GL_NO_ERROR);
    bool cacheHit=false;loadTextureThumbnail(assets,"red.tga",&cacheHit);assert(cacheHit);previews.clear();std::filesystem::remove_all(temp);
    renderer.shutdown();SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();
}
