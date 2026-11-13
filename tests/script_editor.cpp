#include "editor/script_editor.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <cstdlib>

static std::string clipboard;
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
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn=+[](ImGuiContext*,const char* text){clipboard=text;};
    ScriptEditor editor;
    require(editor.source().empty(),"new editor has no example snippet");
    require(!editor.fileOpen() && !editor.executeSource(),"no anonymous script document is executable");
    World fileWorld;auto fileOwner=fileWorld.create();fileWorld.add(fileOwner,Transform{});fileWorld.add(fileOwner,LuaScript{"Test.ls",true,{}});
    editor.bindWorld(fileWorld,{},{},{});editor.setAssetContext("Test.ls",fileOwner);
    editor.setSource("print(\"Hello from Lua!\")");
    auto frame=[&] { ImGui::NewFrame(); editor.draw(); ImGui::Render(); };
    frame(); frame();
    editor.setSource("function sample() return 42 end");frame();
    auto* editorWindow=ImGui::FindWindowByName("Script Editor");require(editorWindow!=nullptr,"editor window exists");
    int glyphVertices=0;const auto white=ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    for(auto* child:editorWindow->DC.ChildWindows)if(child->ChildId==editorWindow->GetID("##source")){
        for(const auto& vertex:child->DrawList->VtxBuffer)if(vertex.uv.x!=white.x || vertex.uv.y!=white.y)++glyphVertices;
    }
    int characters=0;for(char c:editor.source())if(c!=' ' && c!='\n' && c!='\r')++characters;
    require(glyphVertices==characters*4,"syntax highlighting renders one glyph layer, not native plus colored duplicates");
    editor.setSource("print(\"Hello from Lua!\")");frame();
    auto click=[&](float x,float y) {
        io.AddMousePosEvent(x,y); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMouseButtonEvent(0,false); frame();
    };
    click(845,54); // Execute in the default top-right Script Editor.
    bool hello=false;
    for (const auto& line:editor.console().lines())
        if (line.kind==LuaConsole::Kind::Output && line.text=="lua > Hello from Lua!") hello=true;
    require(hello,"Execute button sends script output to console");
    for(float x=970;x<1130 && clipboard.empty();x+=8)click(x,492);
    require(clipboard.find("Hello from Lua!")!=std::string::npos,"Copy all copies console output");
    click(840,492); // Clear in the default bottom-right Console.
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
    AssetManager fileAssets;fileAssets.root=std::filesystem::temp_directory_path()/("legacy-script-documents-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    editor.saveFile(fileAssets);require(fileAssets.readScript("Test.ls")=="createGameObject","edited document saves to its script file");
    fileAssets.writeScript("Gameplay/Movement/Player.ls","");fileAssets.writeScript("Tools/Editor.lua","");
    {std::ofstream other(fileAssets.root/"image.png");other<<"not a script";}
    const auto tree=buildScriptTree(fileAssets);
    require(tree.files.size()==1 && tree.files[0]=="Test.ls","script tree excludes non-script files");
    require(tree.folders.at("Gameplay").folders.at("Movement").files[0]=="Player.ls","script tree preserves folders");
    require(tree.folders.at("Tools").files[0]=="Editor.lua","script tree includes Lua files");
    std::filesystem::remove_all(fileAssets.root);

    World world;auto owner=world.create();world.add(owner,Transform{});world.add(owner,LuaScript{"Camera.ls",true,{}});
    editor.bindWorld(world,{},{},{});editor.setAssetContext("Camera.ls",owner);
    editor.setSource("function onStart() self:setPosition(1,2,3) end");
    editor.startPlay();
    require(world.get<Transform>(owner).position==Vec3{},"asset is not executed twice as a global script on Play");
    require(editor.executeSource(),"Execute resolves attachment owner");
    require(world.get<Transform>(owner).position==Vec3{1,2,3},"attached script Execute receives self");
    auto other=world.create();world.add(other,Transform{});world.add(other,LuaScript{"Camera.ls",true,{}});
    editor.setAssetContext("Camera.ls",{});
    require(!editor.executeSource(),"ambiguous attachment requires selection");
    editor.setAssetContext("Camera.ls",other);
    require(editor.executeSource(),"selected attachment resolves ambiguity");
    require(world.get<Transform>(other).position==Vec3{1,2,3},"self is selected owner");
    world.destroy(other);editor.setAssetContext("Camera.ls",other);
    require(editor.executeSource(),"stale selection falls back to unique live owner");
    editor.setAssetContext("missing.ls",owner);
    require(!editor.executeSource(),"unattached asset cannot execute with wrong self");
    ImGui::DestroyContext();
    std::puts("ImGui Execute-to-console and Clear button tests passed");
}
