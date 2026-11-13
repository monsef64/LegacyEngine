#include "editor/input_settings.hpp"
#include "scripting/lua_console.hpp"
#include "audio/audio_clip.hpp"
#include "audio/audio_preview.hpp"
#include "editor/editor_palette.hpp"
#include <cassert>
int main(){
    InputState input;input.enabled=true;input.mouseEnabled=true;input.buttons={{"Jump",SDL_SCANCODE_SPACE},{"Left",SDL_SCANCODE_A},{"Right",SDL_SCANCODE_D},{"Down",SDL_SCANCODE_S},{"Up",SDL_SCANCODE_W}};
    SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=SDL_SCANCODE_SPACE;input.event(event);assert(input.button("Jump",0) && input.button("Jump",1));
    LuaConsole console;console.execute(R"(
      assert(isKeyPressed("Space") and isKeyDown("Space"))
      assert(isButtonPressed("Jump") and isButtonDown("Jump"))
      assert(not isButtonDown("Undefined"))
      assert(not isKeyDown(999999))
    )");
    input.pressed.fill(false);event.key.repeat=true;input.event(event);assert(!input.pressed[SDL_SCANCODE_SPACE]);
    event.type=SDL_EVENT_KEY_UP;input.event(event);assert(input.button("Jump",2));
    console.execute("assert(isKeyReleased('Space') and isButtonReleased('Jump'))");
    event={};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.button=SDL_BUTTON_LEFT;input.event(event);
    event.type=SDL_EVENT_MOUSE_MOTION;event.motion.x=10;event.motion.y=20;event.motion.xrel=3;event.motion.yrel=-2;input.event(event);
    event.type=SDL_EVENT_MOUSE_WHEEL;event.wheel.x=1;event.wheel.y=2;input.event(event);
    input.keys[SDL_SCANCODE_D]=true;input.keys[SDL_SCANCODE_W]=true;
    input.pad[SDL_GAMEPAD_BUTTON_SOUTH]=input.padPressed[SDL_GAMEPAD_BUTTON_SOUTH]=true;input.axes[0]=.5f;input.axes[1]=-.25f;
    console.execute(R"(
      assert(isMouseButtonPressed('left') and isMouseButtonDown(1))
      assert(getMousePosition().x==10 and getMousePosition().y==20)
      assert(getMouseDelta().x==3 and getMouseWheelDelta().y==2)
      assert(getInputAxis('Left','Right')==1)
      local v=getInputVector('Left','Right','Down','Up')
      assert(v.x>.7 and v.x<.71 and v.y==v.x)
      assert(isGamepadButtonPressed('south') and isGamepadButtonDown('south'))
      assert(getGamepadAxis('leftx')==.5 and getGamepadStick('left').y==-.25)
    )");
    event.type=SDL_EVENT_MOUSE_BUTTON_UP;event.button.button=SDL_BUTTON_LEFT;input.event(event);console.execute("assert(isMouseButtonReleased('left'))");
    event.type=SDL_EVENT_WINDOW_FOCUS_LOST;input.event(event);assert(!input.keys[SDL_SCANCODE_D]);
    for(const auto& line:console.lines()){if(line.kind==LuaConsole::Kind::Error)std::fprintf(stderr,"%s\n",line.text.c_str());assert(line.kind!=LuaConsole::Kind::Error);}
    const auto temp=std::filesystem::temp_directory_path()/("legacy-input-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));AssetManager assets;assets.root=temp/"Assets";
    InputSettings settings;settings.load(assets,input);settings.waiting="Jump";event={};event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=SDL_SCANCODE_J;assert(settings.capture(event,assets,input));assert(input.buttons["Jump"]==SDL_SCANCODE_J && settings.waiting.empty());
    settings.waiting="Cancel";event.key.scancode=SDL_SCANCODE_ESCAPE;assert(settings.capture(event,assets,input));assert(!input.buttons.count("Cancel"));
    assert(readJsonFile(InputSettings::settingsPath(assets),64*1024).at("buttons").at("Jump").at(0)==SDL_SCANCODE_J);
    InputSettings restored;restored.load(assets,input);assert(input.buttons["Jump"]==SDL_SCANCODE_J);
    settings.waiting="Jump";settings.waitingSlot=1;event={};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.button=SDL_BUTTON_RIGHT;
    assert(settings.capture(event,assets,input));assert(input.binding("Jump",1)==InputState::mouseCode+SDL_BUTTON_RIGHT);
    settings.waiting="Jump";settings.waitingSlot=2;event={};event.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN;event.gbutton.button=SDL_GAMEPAD_BUTTON_SOUTH;
    assert(settings.capture(event,assets,input));assert(input.binding("Jump",2)==InputState::gamepadCode+SDL_GAMEPAD_BUTTON_SOUTH);
    InputSettings allSlots;allSlots.load(assets,input);assert(input.binding("Jump",1)==1027 && input.binding("Jump",2)==2048);
    input.enabled=true;input.mouseEnabled=true;input.mouse[3]=input.mousePressed[3]=true;
    assert(input.button("Jump",0) && input.button("Jump",1));input.mouse.fill(false);input.mousePressed.fill(false);
    input.pad[SDL_GAMEPAD_BUTTON_SOUTH]=input.padPressed[SDL_GAMEPAD_BUTTON_SOUTH]=true;assert(input.button("Jump",0) && input.button("Jump",1));
    input.pad.fill(false);input.padPressed.fill(false);input.padReleased[SDL_GAMEPAD_BUTTON_SOUTH]=true;assert(input.button("Jump",2));input.clear();
    assert(console.execute("assert(not isMouseLocked()); assert(not lockMouse()); assert(not hideMouse())"));
    AssetManager savedAs;savedAs.root=temp/"saved-as"/"Assets";assets.copyTo(savedAs.root);settings.save(savedAs,input);InputSettings copied;copied.load(savedAs,input);assert(input.buttons["Jump"]==SDL_SCANCODE_J);
    AssetManager other;other.root=temp/"other";restored.load(other,input);assert(input.buttons.empty());
    std::filesystem::create_directories(other.root);{std::ofstream invalid(other.root/".legacy-input");invalid<<"\"Broken\"";}InputSettings invalid;invalid.load(other,input);assert(!invalid.error.empty() && input.buttons.empty());
    input.enabled=false;event={};event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=SDL_SCANCODE_SPACE;input.event(event);assert(!input.keys[SDL_SCANCODE_SPACE]);
    auto folder=assets.createFolder();assert(folder=="New Folder");auto another=assets.createFolder();assert(another=="New Folder (2)");
    assert(assets.createFolder(folder)=="New Folder/New Folder");
    bool badFolder=false;try{assets.createFolder("../escape");}catch(...){badFolder=true;}assert(badFolder);
    AssetManager fixtures;fixtures.root=TEST_FIXTURES;auto samples=decodeAudioClip(fixtures,"tone.mp3");assert(samples.size()>4800);bool audible=false;for(float s:samples){assert(std::isfinite(s));audible|=std::abs(s)>.001f;}assert(audible);
    {AudioPreview preview(true);preview.toggle(fixtures,"tone.mp3");assert(preview.error.empty());preview.stop();}
    std::filesystem::create_directories(assets.root);std::ofstream bad(assets.root/"bad.mp3");bad<<"not mp3";bad.close();bool rejected=false;try{decodeAudioClip(assets,"bad.mp3");}catch(...){rejected=true;}assert(rejected);
    ImGui::CreateContext();ImGui::StyleColorsDark();auto original=ImGui::GetStyle().Colors[ImGuiCol_Button];applyEditorPalette(1);auto grey=ImGui::GetStyle().Colors[ImGuiCol_Button];assert(grey.x==grey.y && grey.y==grey.z);applyEditorPalette(0);assert(ImGui::GetStyle().Colors[ImGuiCol_Button].x==original.x);ImGui::DestroyContext();
    SDL_Quit();std::filesystem::remove_all(temp);
}
