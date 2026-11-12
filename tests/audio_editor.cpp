#include "audio/audio_system.hpp"
#include "audio/audio_preview.hpp"
#include "scripting/lua_console.hpp"
#include "scripting/lua_highlight.hpp"
#include "editor/rotation_editor.hpp"
#include "project/project_io.hpp"
#include "editor/editor_history.hpp"
#include <cassert>
int main(){
    RotationEditorState rotation;rotation.apply({900,-900,10000});assert(rotation.angles.x==360 && rotation.angles.y==-360 && rotation.angles.z==360);
    auto tokens=luaTokens("function onStart() -- comment\nself:play('sound') --[=[multi\nline]=]\nend");
    bool comment=false,string=false,keyword=false;for(auto t:tokens){comment|=t.kind==LuaToken::Comment;string|=t.kind==LuaToken::String;keyword|=t.kind==LuaToken::Keyword;}assert(comment && string && keyword);
    World world;auto e=world.create();world.add(e,Transform{});LuaConsole console;console.bindWorld(world);assert(console.bindObject(e));
    console.execute(R"(
      local audio = createAudioSource(self, AudioClip("tone.wav"))
      assert(self:getComponent(AudioSource) ~= nil)
      assert(audio:setPitch(2)==2)
      assert(audio:setVolume(0.5)==0.5)
      assert(audio:setPan(-1)==-1)
      assert(audio:setDistance(4)==4)
      assert(audio:spatialBlend(0.75)==0.75)
      assert(audio:setSpatialBlend(2)==1)
      self:addComponent(AudioListener)
      self:getComponent(AudioListener).enabled = true
      assert(audio:setLooping(true))
      audio:play()
      assert(audio:isPlaying())
      audio:pause()
      assert(not audio:isPlaying())
      audio:resume()
      assert(audio:isPlaying())
      audio:mute()
      audio:unmute()
      audio:fadeIn(1)
      audio:fadeOut(1)
      audio:playOneShot(AudioClip("tone.wav"))
    )");
    for(const auto& line:console.lines())assert(line.kind!=LuaConsole::Kind::Error);
    auto& a=world.get<AudioSource>(e);assert(a.pitch==2 && a.volume==.5f && a.shots.size()==1 && a.clip=="tone.wav");
    a.stop();assert(!a.isPlaying());
    ProjectIO::Meshes meshes{};auto encoded=ProjectIO::encode(world,"",OrbitCamera{},meshes);ProjectIO::Loaded loaded;ProjectIO::decode(encoded,loaded,meshes);
    auto& copy=loaded.world.get<AudioSource>(loaded.world.entities()[0]);assert(copy.clip==a.clip && copy.looping && copy.volume==a.volume && copy.blend==a.blend && loaded.world.has<AudioListener>(loaded.world.entities()[0]) && !copy.isPlaying());
    EditorHistory history(world);a.volume=.2;history.commit(world,e);Entity selection;history.undo(world,selection);assert(world.get<AudioSource>(e).volume==.5f);
    const auto temp=std::filesystem::temp_directory_path()/("legacy-audio-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp);AssetManager assets;assets.root=temp;
    // Minimal PCM mono WAV, converted by SDL to stereo float at 48kHz.
    std::ofstream wav(temp/"tone.wav",std::ios::binary);auto word=[&](unsigned v,int bytes){for(int i=0;i<bytes;++i)wav.put(char(v>>(8*i)));};
    wav.write("RIFF",4);word(36+9600,4);wav.write("WAVEfmt ",8);word(16,4);word(1,2);word(1,2);word(48000,4);word(96000,4);word(2,2);word(16,2);wav.write("data",4);word(9600,4);for(int i=0;i<4800;++i)word(1000,2);wav.close();
    auto& source=world.get<AudioSource>(e);source.pan=0;source.volume=1;source.pitch=1;source.looping=false;source.blend=0;source.clip="tone.wav";
    {AudioSystem mixer;
      world.remove<AudioListener>(e);source.play();
      auto silent=mixer.render(world,assets,120);for(float sample:silent)assert(sample==0);
      auto listener=world.create();world.add(listener,Transform{});world.add(listener,AudioListener{});
      assert(activeAudioListener(world)==listener);
      source.fadeIn(.01f);auto in=mixer.render(world,assets,1200);
      assert(in.front()==0 && in[1100]>0 && in.back()>0 && source.isPlaying());
      mixer.render(world,assets,4800);assert(!source.isPlaying());
      source.play();source.fadeOut(.01f);auto out=mixer.render(world,assets,1200);
      assert(out.front()>0 && out.back()==0 && !source.isPlaying());
      source.play();source.pause();source.fadeOut(.01f);mixer.render(world,assets,1200);assert(source.state==AudioSource::State::Paused);source.resume();mixer.render(world,assets,1200);assert(!source.isPlaying());
      source.blend=1;source.distance=10;
      auto& emitter=world.get<Transform>(e);auto& receiver=world.get<Transform>(listener);
      receiver.position={0,0,5};emitter.position={0,0,0};
      auto near=spatialAudioGains(source,&emitter,&receiver);assert(std::abs(near.left-.5f)<.0001f && std::abs(near.right-.5f)<.0001f);
      receiver.position.z=10;auto far=spatialAudioGains(source,&emitter,&receiver);assert(far.left==0 && far.right==0);
      source.blend=0;auto flat=spatialAudioGains(source,&emitter,&receiver);assert(flat.left==1 && flat.right==1);
      source.blend=.5;auto mixed=spatialAudioGains(source,&emitter,&receiver);assert(mixed.left==.5f && mixed.right==.5f);
      source.blend=1;receiver.position={-5,0,0};auto right=spatialAudioGains(source,&emitter,&receiver);assert(right.left==0 && right.right>0);
      world.get<AudioListener>(listener).enabled=false;assert(activeAudioListener(world)==Entity{});
      source.play();auto muted=mixer.render(world,assets,120);for(float sample:muted)assert(sample==0);
      source.distance=0;receiver.position={0,0,1};assert(spatialAudioGains(source,&emitter,&receiver).left==0);
      source.stop();source.blend=0;world.get<AudioListener>(listener).enabled=true;
    }
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    {AudioSystem system;auto& source=world.get<AudioSource>(e);source.playOnStart=true;source.looping=false;source.pitch=1;system.start(world);system.update(world,assets,true);assert(system.error.empty());assert(source.isPlaying());
     source.pause();system.update(world,assets,false);assert(!source.isPlaying());source.resume();system.clear();source.stop();source.playOneShot("../bad.wav");system.update(world,assets,true);assert(!system.error.empty());system.clear();}
    {AudioPreview preview;preview.toggle(assets,"tone.wav");assert(preview.error.empty() && preview.playing("tone.wav"));preview.toggle(assets,"tone.wav");assert(!preview.playing("tone.wav"));}
    SDL_Quit();std::filesystem::remove_all(temp);
}
