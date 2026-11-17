#include "editor/script_editor.hpp"
#include "ui/mygui_ui.hpp"
#include <cstdio>
#include <cstdlib>

static std::string clipboard;
static void require(bool ok,const char* message)
{
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main()
{
    ui::createContext();
    ui::setDisplaySize(1280, 720);
    ui::setDeltaTime(1.0f / 60.0f);
    ui::setClipboardCallbacks([](const std::string& text) { clipboard = text; },
                              [] { return clipboard; });
    ScriptEditor editor;
    require(editor.source().empty(),"new editor has no example snippet");
    require(!editor.fileOpen() && !editor.executeSource(),"no anonymous script document is executable");
    World fileWorld;auto fileOwner=fileWorld.create();fileWorld.add(fileOwner,Transform{});fileWorld.add(fileOwner,LuaScript{"Test.ls",true,{}});
    editor.bindWorld(fileWorld,{},{},{});editor.setAssetContext("Test.ls",fileOwner);
    editor.setSource("print(\"Hello from Lua!\")");
    auto frame=[&] { ui::newFrame(); editor.draw(); ui::render(); };
    frame(); frame();
    editor.setSource("function sample() return 42 end");frame();
    require(ui::lastFrameVertexCount() > 0,
            "script editor and Lua syntax overlay produce native UI geometry");
    editor.setSource("print(\"Hello from Lua!\")");frame();
    auto click=[&](float x,float y) {
        ui::injectMousePos(x,y); frame();
        ui::injectMouseButton(0,true); frame();
        ui::injectMouseButton(0,false); frame();
    };
    click(845,102); // Execute in the default top-right Script Editor.
    bool hello=false;
    for (const auto& line:editor.console().lines())
        if (line.kind==LuaConsole::Kind::Output && line.text=="lua > Hello from Lua!") hello=true;
    require(hello,"Execute button sends script output to console");
    for(float x=970;x<1130 && clipboard.empty();x+=8)click(x,492);
    require(clipboard.find("Hello from Lua!")!=std::string::npos,"Copy all copies console output");
    click(840,492); // Clear in the default bottom-right Console.
    require(editor.console().lines().empty(),"console Clear button empties transcript");
    ui::injectMousePos(900,180); frame();
    ui::injectMouseButton(0,true); frame();
    ui::injectMouseButton(0,false); frame();
    ui::injectKeyMod(ui::Mod_Ctrl,true); ui::injectKey(ui::Key_A,true); frame();
    ui::injectKey(ui::Key_A,false); ui::injectKeyMod(ui::Mod_Ctrl,false); frame();
    ui::injectTextUtf8("--é\ncreateGx"); frame();
    ui::injectKey(ui::Key_LeftArrow,true); frame();
    ui::injectKey(ui::Key_LeftArrow,false); frame();
    ui::injectKey(ui::Key_Tab,true); frame();
    ui::injectKey(ui::Key_Tab,false); frame();
    ui::injectTextUtf8("!"); frame();
    require(editor.source()=="--é\ncreateGameObject!x",
            "Tab completion preserves UTF-8 cursor offsets and inserts at the caret");
    AssetManager fileAssets;fileAssets.root=std::filesystem::temp_directory_path()/("legacy-script-documents-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    editor.saveFile(fileAssets);
    require(fileAssets.readScript("Test.ls") == "--é\ncreateGameObject!x",
            "edited document saves to its script file");
    fileAssets.writeScript("Gameplay/Movement/Player.ls","");fileAssets.writeScript("Tools/Editor.lua","");
    {std::ofstream other(fileAssets.root/"image.png");other<<"not a script";}
    const auto tree=buildScriptTree(fileAssets);
    require(tree.files.size()==1 && tree.files[0]=="Test.ls","script tree excludes non-script files");
    require(tree.folders.at("Gameplay").folders.at("Movement").files[0]=="Player.ls","script tree preserves folders");
    require(tree.folders.at("Tools").files[0]=="Editor.lua","script tree includes Lua files");
    std::filesystem::remove_all(fileAssets.root);

    // Exercise the same Lua overlay helper used by ScriptEditor against MyGUI's
    // reported scroll offsets, including an edit while each axis is scrolled.
    ui::destroyContext();
    require(ui::createContext(), "fresh MyGUI context for syntax alignment checks");
    ui::setDisplaySize(1280, 720);
    ui::setDeltaTime(1.0f / 60.0f);
    float syntaxGutterWidth = 0.0f;
    struct SyntaxSnapshot {
        ui::MultilineEditInfo edit;
        bool keywordFound = false;
        bool apiFound = false;
        bool clipFound = false;
        ui::Vec2 keywordPosition{};
        ui::Vec2 apiPosition{};
        ui::DrawClipRect clip{};
    };
    auto syntaxFrame = [&](const char* windowName,
                           char* buffer,
                           std::size_t bufferSize,
                           ui::Vec2 editSize,
                           bool requestFocus,
                           SyntaxSnapshot& snapshot) {
        ui::newFrame();
        ui::setNextWindowPos(ui::Vec2(80, 60), ui::Cond_Always);
        ui::setNextWindowSize(ui::Vec2(360, 240), ui::Cond_Always);
        ui::beginWindow(windowName);
        if (requestFocus) ui::setKeyboardFocusHere();
        ui::MultilineEditInfo editInfo;
        ui::inputTextMultiline("##source",
                               buffer,
                               bufferSize,
                               editSize,
                               ui::InputTextFlags_None,
                               nullptr,
                               nullptr,
                               &editInfo,
                               syntaxGutterWidth);
        auto* draw = ui::windowDrawList();
        const auto vertexStart = draw->vertices().size();
        script_editor_detail::drawLuaSyntaxOverlay(draw, buffer, editInfo);
        const auto vertexEnd = draw->vertices().size();
        snapshot = {};
        snapshot.edit = editInfo;
        constexpr uint32_t keywordColor = UI_COL32(195, 140, 245, 255);
        constexpr uint32_t apiColor = UI_COL32(110, 175, 240, 255);
        const auto display = ui::io().DisplaySize;
        for (std::size_t i = vertexStart; i < vertexEnd; ++i) {
            const auto& vertex = draw->vertices()[i];
            const ui::Vec2 position{(vertex.x + 1.0f) * display.x * 0.5f,
                                    (1.0f - vertex.y) * display.y * 0.5f};
            if (!snapshot.keywordFound && vertex.colour == keywordColor) {
                snapshot.keywordPosition = position;
                snapshot.keywordFound = true;
            }
            if (!snapshot.apiFound && vertex.colour == apiColor) {
                snapshot.apiPosition = position;
                snapshot.apiFound = true;
            }
        }
        for (const auto& command : draw->commands()) {
            if (command.vertexOffset <= vertexStart &&
                command.vertexOffset + command.vertexCount >= vertexEnd) {
                snapshot.clip = command.clip;
                snapshot.clipFound = command.hasClip &&
                                     std::abs(command.clip.minX - editInfo.innerRect.x) < 1.0f &&
                                     std::abs(command.clip.minY - editInfo.innerRect.y) < 1.0f &&
                                     std::abs(command.clip.maxX -
                                              (editInfo.innerRect.x + editInfo.innerRect.width)) < 1.0f &&
                                     std::abs(command.clip.maxY -
                                              (editInfo.innerRect.y + editInfo.innerRect.height)) < 1.0f;
                break;
            }
        }
        ui::endWindow();
        ui::render();
    };

    std::string horizontalSource = "local " + std::string(120, 'x');
    char horizontalBuffer[512]{};
    std::snprintf(horizontalBuffer, sizeof(horizontalBuffer), "%s", horizontalSource.c_str());
    SyntaxSnapshot horizontalHome, horizontalScrolled;
    syntaxFrame("Horizontal syntax alignment", horizontalBuffer, sizeof(horizontalBuffer),
                ui::Vec2(240, 90), true, horizontalHome);
    ui::injectKeyMod(ui::Mod_Ctrl, true);
    ui::injectKey(ui::Key_Home, true);
    syntaxFrame("Horizontal syntax alignment", horizontalBuffer, sizeof(horizontalBuffer),
                ui::Vec2(240, 90), false, horizontalHome);
    ui::injectKey(ui::Key_Home, false);
    ui::injectKeyMod(ui::Mod_Ctrl, false);
    syntaxFrame("Horizontal syntax alignment", horizontalBuffer, sizeof(horizontalBuffer),
                ui::Vec2(240, 90), false, horizontalHome);
    ui::injectKey(ui::Key_End, true);
    syntaxFrame("Horizontal syntax alignment", horizontalBuffer, sizeof(horizontalBuffer),
                ui::Vec2(240, 90), false, horizontalScrolled);
    ui::injectKey(ui::Key_End, false);
    syntaxFrame("Horizontal syntax alignment", horizontalBuffer, sizeof(horizontalBuffer),
                ui::Vec2(240, 90), false, horizontalScrolled);
    ui::injectTextUtf8("z");
    SyntaxSnapshot horizontalEdited;
    syntaxFrame("Horizontal syntax alignment", horizontalBuffer, sizeof(horizontalBuffer),
                ui::Vec2(240, 90), false, horizontalEdited);
    require(horizontalHome.keywordFound && horizontalHome.clipFound &&
                horizontalEdited.keywordFound && horizontalEdited.clipFound &&
                horizontalEdited.edit.scrollX > horizontalHome.edit.scrollX + 20.0f &&
                std::abs((horizontalHome.keywordPosition.x - horizontalEdited.keywordPosition.x) -
                         (horizontalEdited.edit.scrollX - horizontalHome.edit.scrollX)) < 2.0f &&
                std::abs(horizontalHome.keywordPosition.y - horizontalEdited.keywordPosition.y) < 1.0f &&
                std::string(horizontalBuffer).back() == 'z',
            "Lua keyword overlays track MyGUI horizontal scrolling while text is edited");

    syntaxGutterWidth = 48.0f;
    std::string verticalSource;
    for (int line = 0; line < 14; ++line) {
        verticalSource += line == 2 ? "Camera = 1\n" : "-- filler\n";
    }
    verticalSource += "return true";
    char verticalBuffer[512]{};
    std::snprintf(verticalBuffer, sizeof(verticalBuffer), "%s", verticalSource.c_str());
    SyntaxSnapshot verticalBase, verticalScrolled;
    syntaxFrame("Vertical syntax alignment", verticalBuffer, sizeof(verticalBuffer),
                ui::Vec2(240, 70), false, verticalBase);
    syntaxFrame("Vertical syntax alignment", verticalBuffer, sizeof(verticalBuffer),
                ui::Vec2(240, 70), true, verticalBase);
    ui::injectKeyMod(ui::Mod_Ctrl, true);
    ui::injectKey(ui::Key_Home, true);
    syntaxFrame("Vertical syntax alignment", verticalBuffer, sizeof(verticalBuffer),
                ui::Vec2(240, 70), false, verticalBase);
    ui::injectKey(ui::Key_Home, false);
    ui::injectKeyMod(ui::Mod_Ctrl, false);
    syntaxFrame("Vertical syntax alignment", verticalBuffer, sizeof(verticalBuffer),
                ui::Vec2(240, 70), false, verticalBase);
    for (int i = 0; i < 6; ++i) {
        ui::injectKey(ui::Key_DownArrow, true);
        syntaxFrame("Vertical syntax alignment", verticalBuffer, sizeof(verticalBuffer),
                    ui::Vec2(240, 70), false, verticalScrolled);
        ui::injectKey(ui::Key_DownArrow, false);
        syntaxFrame("Vertical syntax alignment", verticalBuffer, sizeof(verticalBuffer),
                    ui::Vec2(240, 70), false, verticalScrolled);
    }
    ui::injectTextUtf8("x");
    SyntaxSnapshot verticalEdited;
    syntaxFrame("Vertical syntax alignment", verticalBuffer, sizeof(verticalBuffer),
                ui::Vec2(240, 70), false, verticalEdited);
    require(verticalBase.apiFound && verticalBase.clipFound &&
                verticalEdited.apiFound && verticalEdited.clipFound &&
                verticalBase.edit.gutterRect.width >= 47.0f &&
                verticalBase.edit.innerRect.x >=
                    verticalBase.edit.gutterRect.x + verticalBase.edit.gutterRect.width &&
                verticalBase.edit.textOrigin.x >= verticalBase.edit.innerRect.x &&
                verticalEdited.edit.scrollY > 0.0f &&
                std::abs((verticalBase.apiPosition.y - verticalEdited.apiPosition.y) -
                         verticalEdited.edit.scrollY) < 2.0f &&
                verticalBuffer[0] == '-',
            "Lua API overlays track MyGUI vertical scrolling while text is edited");

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
    ui::destroyContext();
    std::puts("MyGUI Execute-to-console and Clear button tests passed");
}
