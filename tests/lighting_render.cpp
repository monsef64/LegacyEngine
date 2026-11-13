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
    // Game debug collider overlays use the Game camera and work without mesh
    // geometry on the collider entity. Drawing is opt-in, after the normal pass.
    const auto withoutOverlay=read();auto colliderOnly=world.create();world.add(colliderOnly,Transform{{0,2,0}});world.add(colliderOnly,Collider{});
    world.each<Transform,Collider>([&](Entity,const Transform& transform,const Collider& collider){renderer.drawCollider(transform,collider,vp);});glFinish();
    std::vector<unsigned char> withOverlay(192*192*3);glReadPixels(0,0,192,192,GL_RGB,GL_UNSIGNED_BYTE,withOverlay.data());
    assert(glGetError()==GL_NO_ERROR && difference(withoutOverlay,withOverlay)>100);world.destroy(colliderOnly);
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
    bool cacheHit=false;loadTextureThumbnail(assets,"red.tga",&cacheHit);assert(cacheHit);previews.clear();
    renderer.setAssetRoot(assets.root);
    MaterialProperties material;material.texture="red.tga";saveMaterial(assets,"Test.mat",material);
    const auto plain=read();world.get<MeshRenderer>(box).material="Test.mat";world.get<MeshRenderer>(floor).material="Test.mat";
    const auto textured=read();assert(difference(plain,textured)>1000);
    auto& mesh=world.get<MeshRenderer>(floor);mesh.materialValues.normalMap="normal.tga";mesh.materialValues.shader=1;mesh.materialOverrides=MaterialNormal|MaterialShader;
    {std::ofstream image(assets.root/"normal.tga",std::ios::binary);unsigned char header[18]{};header[2]=2;header[12]=2;header[14]=2;header[16]=24;image.write(reinterpret_cast<char*>(header),18);for(int i=0;i<4;++i){image.put(char(192));image.put(char(128));image.put(char(240));}}
    DirectionalLight lit;lit.shadows=false;world.add(lamp,lit);
    mesh.materialOverrides=0;const auto diffuse=read();mesh.materialOverrides=MaterialNormal|MaterialShader;const auto bumped=read();assert(difference(diffuse,bumped)>100);
    assert(renderer.materialError.empty());
    mesh.materialValues.texture="missing.png";mesh.materialOverrides|=MaterialTexture;read();assert(!renderer.materialError.empty());
    // Texture-alpha Diffuse remains lit, sorts independently of entity order,
    // leaves opaque depth intact, and restores GL blending/depth-write state.
    auto rgbaImage=[&](const char* name,unsigned char r,unsigned char g,unsigned char b,unsigned char alpha){
        std::ofstream image(assets.root/name,std::ios::binary);unsigned char header[18]{};header[2]=2;header[12]=2;header[14]=2;header[16]=32;header[17]=8;image.write(reinterpret_cast<char*>(header),18);
        for(int i=0;i<4;++i){image.put(char(b));image.put(char(g));image.put(char(r));image.put(char(alpha));}
    };
    rgbaImage("alpha-red.tga",255,0,0,128);rgbaImage("alpha-green.tga",0,255,0,128);rgbaImage("alpha-zero.tga",255,0,0,0);
    World alphaWorld;auto farGlass=alphaWorld.create(),nearGlass=alphaWorld.create();
    MeshRenderer glass;glass.mesh=cube;glass.materialValues.shader=2;glass.materialValues.texture="alpha-red.tga";glass.materialOverrides=MaterialShader|MaterialTexture;
    world.get<MeshRenderer>(floor).materialOverrides=0;
    auto greenGlass=glass;greenGlass.materialValues.texture="alpha-green.tga";
    alphaWorld.add(farGlass,Transform{{0,0,-2}});alphaWorld.add(farGlass,greenGlass);alphaWorld.add(nearGlass,Transform{});alphaWorld.add(nearGlass,glass);
    const Vec3 alphaEye{0,0,6};const auto alphaVP=multiply(perspective(kPi/3,1,.1f,100),lookAt(alphaEye,{0,0,0},{0,1,0}));
    auto alphaRead=[&](){renderer.draw(alphaWorld,alphaVP,192,192,alphaEye,0,0,false);glFinish();std::array<unsigned char,4> p{};glReadPixels(96,96,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());GLboolean depth=0;glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);assert(depth && !glIsEnabled(GL_BLEND) && glGetError()==GL_NO_ERROR);return p;};
    auto blended=alphaRead();assert(blended[0]>blended[1] && blended[1]>blended[2]);
    // Swap the objects' positions and material values, not their iteration order.
    alphaWorld.get<Transform>(farGlass).position={0,0,0};alphaWorld.get<MeshRenderer>(farGlass)=glass;
    alphaWorld.get<Transform>(nearGlass).position={0,0,-2};alphaWorld.get<MeshRenderer>(nearGlass)=greenGlass;
    assert(alphaRead()==blended);
    auto lightEntity=alphaWorld.create();alphaWorld.add(lightEntity,Transform{{0,0,4}});
    auto lightingCheck=[&](auto light){using T=decltype(light);light.shadows=false;light.intensity=2;alphaWorld.add(lightEntity,light);auto lit=alphaRead();alphaWorld.remove<T>(lightEntity);assert(lit[0]>blended[0]+5);};
    lightingCheck(DirectionalLight{});lightingCheck(PointLight{});lightingCheck(SpotLight{});
    alphaWorld.get<MeshRenderer>(farGlass).materialValues.texture="alpha-zero.tga";alphaWorld.get<MeshRenderer>(nearGlass).materialValues.texture="alpha-zero.tga";
    auto clear=alphaRead();assert(clear[0]>=24 && clear[0]<=27); // clear background, no opaque silhouette
    auto occluder=alphaWorld.create();alphaWorld.add(occluder,Transform{{0,0,2}});alphaWorld.add(occluder,MeshRenderer{cube,{0,0,1}});
    alphaWorld.get<MeshRenderer>(farGlass).materialValues.texture="alpha-red.tga";
    auto occluded=alphaRead();assert(occluded[0]<3 && occluded[1]<3 && occluded[2]>25);
    // Six independently colored faces verify axis mapping, camera translation
    // invariance, color tinting, HDR decoding and foreground depth occlusion.
    World skyWorld;auto skyEntity=skyWorld.create();skyWorld.add(skyEntity,Skybox{});
    auto& sky=skyWorld.get<Skybox>(skyEntity);
    const unsigned char colors[6][3]{{255,0,0},{0,255,0},{0,0,255},{255,255,0},{255,0,255},{0,255,255}};
    int faceIndex=0;for(auto* path:{&sky.right,&sky.left,&sky.top,&sky.bottom,&sky.front,&sky.back}){
        *path="sky"+std::to_string(faceIndex)+".tga";
        std::ofstream image(assets.root/ *path,std::ios::binary);unsigned char header[18]{};header[2]=2;header[12]=2;header[14]=2;header[16]=24;image.write(reinterpret_cast<char*>(header),18);
        for(int i=0;i<4;++i){image.put(char(colors[faceIndex][2]));image.put(char(colors[faceIndex][1]));image.put(char(colors[faceIndex][0]));}++faceIndex;
    }
    auto skyRead=[&](Vec3 position,Vec3 direction,Vec3 up=Vec3{0,1,0}){
        auto matrix=multiply(perspective(kPi/3,1,.1f,100),lookAt(position,position+direction,up));
        renderer.draw(skyWorld,matrix,192,192,position,0,0,false);glFinish();assert(glGetError()==GL_NO_ERROR);
        std::array<unsigned char,3> pixel{};glReadPixels(96,96,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixel.data());return pixel;
    };
    const Vec3 directions[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,-1},{0,0,1}};
    for(int i=0;i<6;++i){auto pixel=skyRead({},directions[i],i==2 || i==3?Vec3{0,0,1}:Vec3{0,1,0});for(int c=0;c<3;++c)assert(std::abs(int(pixel[c])-colors[i][c])<=1);}
    assert(skyRead({20,10,30},{1,0,0})==skyRead({},{1,0,0}));
    sky.color={.5f,1,1};auto tinted=skyRead({},{1,0,0});assert(tinted[0]>=126 && tinted[0]<=129);sky.color={1,1,1};
    {std::ofstream hdr(assets.root/"studio.hdr",std::ios::binary);hdr<<"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";for(int i=0;i<4;++i){hdr.put(char(128));hdr.put(char(128));hdr.put(char(128));hdr.put(char(129));}}
    sky.type=1;sky.hdr="studio.hdr";auto hdrPixel=skyRead({},{0,0,-1});for(auto c:hdrPixel)assert(c>=184 && c<=188);assert(renderer.skyboxError().empty());
    auto foreground=skyWorld.create();skyWorld.add(foreground,Transform{{0,0,-3}});skyWorld.add(foreground,MeshRenderer{cube,{1,0,0}});
    auto objectPixel=skyRead({},{0,0,-1});assert(objectPixel[1]<10 && objectPixel[2]<10);skyWorld.destroy(foreground);
    sky.enabled=false;auto disabled=skyRead({},{0,0,-1});assert(disabled[0]<40 && disabled[1]<40);
    std::filesystem::remove_all(temp);
    renderer.shutdown();SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();
}
