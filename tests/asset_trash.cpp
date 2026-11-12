#include "asset_browser.hpp"
#include "level_objects.hpp"
#include <cassert>

int main() {
#if defined(__linux__)
    const auto temp=std::filesystem::temp_directory_path()/
        ("legacy-trash-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp/"Assets");
    const char* old=std::getenv("XDG_DATA_HOME");const bool hadOld=old;const std::string oldValue=old?old:"";
    // Never touch the user's actual Trash during tests.
    setenv("XDG_DATA_HOME",(temp/"DesktopData").c_str(),1);
    const auto path=temp/"Assets"/"My script #1.lua";
    {std::ofstream out(path);out<<"original contents";}
    World world;auto object=world.create();world.add(object,Transform{});
    EditorHistory history(world,object);
    auto receipt=std::make_shared<TrashFile>(path);
    assert(history.perform(world,object,[receipt](bool redo){if(redo)receipt->remove();else receipt->restore();}));
    assert(!std::filesystem::exists(path));assert(std::filesystem::exists(receipt->trashed));
    {std::ifstream in(receipt->info);std::string metadata{std::istreambuf_iterator<char>(in),{}};
        assert(metadata.find("My%20script%20%231.lua")!=std::string::npos);assert(metadata.find("DeletionDate=")!=std::string::npos);}
    world.get<Transform>(object).position.x=7;history.commit(world,object);
    Entity selection=object;
    assert(history.undo(world,selection));assert(!std::filesystem::exists(path));
    assert(world.get<Transform>(object).position.x==0);
    assert(history.undo(world,selection));assert(std::filesystem::exists(path));
    assert(!std::filesystem::exists(receipt->info));
    assert(history.redo(world,selection));assert(!std::filesystem::exists(path));
    assert(history.redo(world,selection));assert(world.get<Transform>(object).position.x==7);
    assert(history.undo(world,selection));
    // Restore conflict must keep the asset and the history entry untouched.
    {std::ofstream out(path);out<<"new asset";}
    assert(!history.undo(world,selection));assert(history.canUndo());assert(!history.error.empty());
    assert(std::filesystem::exists(receipt->trashed));std::filesystem::remove(path);
    assert(history.undo(world,selection));
    {std::ifstream in(path);std::string contents{std::istreambuf_iterator<char>(in),{}};assert(contents=="original contents");}
    assert(history.redo(world,selection));
    // If the desktop empties Trash, fail safely rather than pretending to undo.
    std::filesystem::remove(receipt->trashed);
    assert(!history.undo(world,selection));assert(history.canUndo());
    // UI: list/grid both draw; F2/Delete require Assets focus and do not prompt.
    AssetBrowser browser;browser.assets.root=temp/"Assets";browser.selected="Tile.lua";
    {std::ofstream out(temp/"Assets"/"Tile.lua");out<<"print(1)";}
    ScriptEditor scripts;EditorHistory uiHistory(world,object);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(1000,700);io.DeltaTime=1.0f/60;io.Fonts->Build();
    auto frame=[&](bool assetsFocus) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10,10));ImGui::SetNextWindowSize(ImVec2(650,480));
        if(assetsFocus)ImGui::SetNextWindowFocus();
        browser.draw(nullptr,world,object,scripts,true,&uiHistory);
        ImGui::SetNextWindowPos(ImVec2(700,10));ImGui::SetNextWindowSize(ImVec2(200,200));
        if(!assetsFocus)ImGui::SetNextWindowFocus();
        ImGui::Begin("Other panel");ImGui::TextUnformatted("Focus isolation");ImGui::End();
        ImGui::Render();
    };
    frame(false);browser.setGridMode(true);frame(false);assert(browser.gridMode());
    io.AddKeyEvent(ImGuiKey_Delete,true);frame(false);
    assert(std::filesystem::exists(temp/"Assets"/"Tile.lua"));
    io.AddKeyEvent(ImGuiKey_Delete,false);frame(true);frame(true);
    io.AddKeyEvent(ImGuiKey_F2,true);frame(true);
    assert(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId));
    io.AddKeyEvent(ImGuiKey_F2,false);io.AddKeyEvent(ImGuiKey_Escape,true);frame(true);
    io.AddKeyEvent(ImGuiKey_Escape,false);frame(true);frame(true);
    io.AddKeyEvent(ImGuiKey_Delete,true);frame(true);
    assert(!std::filesystem::exists(temp/"Assets"/"Tile.lua"));
    assert(!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId));
    assert(uiHistory.undo(world,selection));assert(std::filesystem::exists(temp/"Assets"/"Tile.lua"));
    ImGui::DestroyContext();
    if(hadOld)setenv("XDG_DATA_HOME",oldValue.c_str(),1);else unsetenv("XDG_DATA_HOME");
    std::filesystem::remove_all(temp);
#endif
}
