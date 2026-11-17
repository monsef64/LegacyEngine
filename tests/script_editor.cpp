// Script document behaviour: the editor buffer, its Lua execution context and
// the script tree. The widget-level interactions (clicking Execute, scrolling
// the text view) belong to the Qt panels now and are covered by the editor's
// own smoke test; what stays here is the model the panels drive.
#include "editor/script_editor.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

static void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main()
{
    ScriptEditor editor;
    require(editor.source().empty(), "new editor has no example snippet");
    require(!editor.fileOpen() && !editor.executeSource(),
            "no anonymous script document is executable");

    // A script only runs against an attachment, with that object as `self`.
    World fileWorld;
    auto fileOwner = fileWorld.create();
    fileWorld.add(fileOwner, Transform{});
    fileWorld.add(fileOwner, LuaScript{"Test.ls", true, {}});
    editor.bindWorld(fileWorld);
    editor.setAssetContext("Test.ls", fileOwner);

    editor.setSource("print(\"Hello from Lua!\")");
    require(editor.executeSource(), "attached script executes");
    bool hello = false;
    for (const auto& line : editor.console().lines())
        if (line.kind == LuaConsole::Kind::Output && line.text == "lua > Hello from Lua!")
            hello = true;
    require(hello, "script output reaches the console");

    // The document saves to its own script file, byte for byte.
    AssetManager fileAssets;
    fileAssets.root = std::filesystem::temp_directory_path() /
        ("legacy-script-documents-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    editor.setSource("--\xC3\xA9\ncreateGameObject!x");
    editor.markDirty();
    require(editor.hasUnsavedChanges(), "editing marks the document dirty");
    editor.saveFile(fileAssets);
    require(!editor.hasUnsavedChanges(), "saving clears the dirty flag");
    require(fileAssets.readScript("Test.ls") == "--\xC3\xA9\ncreateGameObject!x",
            "edited document saves to its script file");

    // Opening a file loads it back into the same buffer.
    require(editor.loadFile(fileAssets, "Test.ls"), "opening a script asset works");
    require(editor.source() == "--\xC3\xA9\ncreateGameObject!x", "opened file fills the buffer");
    require(!editor.loadFile(fileAssets, "image.png"), "non-script assets are rejected");

    // The tree lists scripts only, and keeps their folders.
    fileAssets.writeScript("Gameplay/Movement/Player.ls", "");
    fileAssets.writeScript("Tools/Editor.lua", "");
    { std::ofstream other(fileAssets.root / "image.png"); other << "not a script"; }
    const auto tree = buildScriptTree(fileAssets);
    require(tree.files.size() == 1 && tree.files[0] == "Test.ls",
            "script tree excludes non-script files");
    require(tree.folders.at("Gameplay").folders.at("Movement").files[0] == "Player.ls",
            "script tree preserves folders");
    require(tree.folders.at("Tools").files[0] == "Editor.lua", "script tree includes Lua files");
    std::filesystem::remove_all(fileAssets.root);

    // Execution context: attached object, ambiguity, and stale selection.
    World world;
    auto owner = world.create();
    world.add(owner, Transform{});
    world.add(owner, LuaScript{"Camera.ls", true, {}});
    editor.bindWorld(world);
    editor.setAssetContext("Camera.ls", owner);
    editor.setSource("function onStart() self:setPosition(1,2,3) end");
    editor.startPlay();
    require(world.get<Transform>(owner).position == Vec3{},
            "asset is not executed twice as a global script on Play");
    require(editor.executeSource(), "Execute resolves attachment owner");
    require(world.get<Transform>(owner).position == Vec3{1, 2, 3},
            "attached script Execute receives self");

    auto other = world.create();
    world.add(other, Transform{});
    world.add(other, LuaScript{"Camera.ls", true, {}});
    editor.setAssetContext("Camera.ls", {});
    require(!editor.executeSource(), "ambiguous attachment requires selection");
    editor.setAssetContext("Camera.ls", other);
    require(editor.executeSource(), "selected attachment resolves ambiguity");
    require(world.get<Transform>(other).position == Vec3{1, 2, 3}, "self is selected owner");
    world.destroy(other);
    editor.setAssetContext("Camera.ls", other);
    require(editor.executeSource(), "stale selection falls back to unique live owner");
    editor.setAssetContext("missing.ls", owner);
    require(!editor.executeSource(), "unattached asset cannot execute with wrong self");
    std::puts("Script document and execution tests passed");
}
