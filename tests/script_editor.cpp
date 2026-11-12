#include "script_editor.hpp"
#include <imgui.h>
#include <cstdio>
#include <cstdlib>

static void require(bool ok,const char* message)
{
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main()
{
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(1280,720);
    io.DeltaTime=1.0f/60;
    unsigned char* pixels; int width,height;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    ScriptEditor editor;
    require(editor.source().empty(),"new editor has no example snippet");
    editor.setSource("print(\"Hello from Lua!\")");
    auto frame=[&] { ImGui::NewFrame(); editor.draw(); ImGui::Render(); };
    frame(); frame();
    auto click=[&](float x,float y) {
        io.AddMousePosEvent(x,y); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMouseButtonEvent(0,false); frame();
    };
    click(845,54); // Execute in the default top-right Lua Editor.
    bool hello=false;
    for (const auto& line:editor.console().lines())
        if (line.kind==LuaConsole::Kind::Output && line.text=="lua > Hello from Lua!") hello=true;
    require(hello,"Execute button sends script output to console");
    click(840,492); // Clear in the default bottom-right Lua Console.
    require(editor.console().lines().empty(),"console Clear button empties transcript");
    io.AddMousePosEvent(900,140); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMouseButtonEvent(0,false); frame();
    io.AddKeyEvent(ImGuiMod_Ctrl,true); io.AddKeyEvent(ImGuiKey_A,true); frame();
    io.AddKeyEvent(ImGuiKey_A,false); io.AddKeyEvent(ImGuiMod_Ctrl,false); frame();
    io.AddInputCharactersUTF8("createG"); frame();
    io.AddKeyEvent(ImGuiKey_Tab,true); frame();
    io.AddKeyEvent(ImGuiKey_Tab,false); frame();
    require(editor.source()=="createGameObject","Tab completes scene API in multiline editor");
    ImGui::DestroyContext();
    std::puts("ImGui Execute-to-console and Clear button tests passed");
}
