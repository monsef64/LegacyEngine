#pragma once
#include "assets/assets.hpp"
#include "assets/cubemap.hpp"
#include "assets/texture_preview.hpp"
#include "audio/audio_preview.hpp"
#include "editor/editor_history.hpp"
#include "editor/editor_layout.hpp"
#include "editor/editor_settings.hpp"
#include "editor/script_editor.hpp"
#include "materials/material_io.hpp"
#include "platform/project_dialog.hpp"
#include "platform/system_trash.hpp"
#include "project/project_assets.hpp"
#include <imgui_internal.h>

// Font Awesome Free Solid glyphs, merged into the embedded editor font.
inline const char* assetIcon(const std::string& path, bool folder = false) {
    if (folder) {
        return "\xef\x81\xbc"; // folder-open
    }
    auto ext = AssetManager::extension(path);
    if (ext == ".ls" || ext == ".lua") {
        return "\xef\x87\x89"; // file-code
    }
    if (ext == ".prefab") {
        return "\xef\x86\xb2";
    }
    if (ext == ".cubemap") {
        return "\xef\x9e\xa2";
    }
    if (ext == ".mat") {
        return "\xef\x81\x82"; // material sphere
    }
    if (textureAsset(path)) {
        return "\xef\x87\x85"; // file-image
    }
    if (ext == ".lemesh" || ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
        return "\xef\x86\xb2"; // cube
    }
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
        return "\xef\x80\xa8"; // volume-high
    }
    if (ext == ".txt" || ext == ".json" || ext == ".md") {
        return "\xef\x85\x9c"; // file-lines
    }
    return "\xef\x85\x9b"; // file
}
inline void
fileBadge(const std::string& path, ImVec2 pos, bool folder = false, unsigned thumbnail = 0) {
    auto* draw = ImGui::GetWindowDrawList();
    if (thumbnail) {
        draw->AddImage(static_cast<ImTextureID>(thumbnail),
                       ImVec2(pos.x, pos.y),
                       ImVec2(pos.x + 32, pos.y + 32));
        return;
    }
    draw->AddRectFilled(pos, ImVec2(pos.x + 32, pos.y + 32), IM_COL32(35, 45, 60, 255), 4);
    const char* icon = assetIcon(path, folder);
    const float width = ImGui::CalcTextSize(icon).x;
    draw->AddText(
        ImVec2(pos.x + (32 - width) * .5f, pos.y + 6), IM_COL32(160, 185, 215, 255), icon);
}
inline void fileIcon(const std::string& path, bool folder = false, unsigned thumbnail = 0) {
    auto pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(36, 32));
    fileBadge(path, pos, folder, thumbnail);
    ImGui::SameLine();
}
class AssetBrowser {
    UntitledProjectAssets staging_;

  public:
    AssetBrowser() {
        assets.root = staging_.path;
    }
    void setGridMode(bool grid) {
        grid_ = grid;
    }
    bool gridMode() const {
        return grid_;
    }
    AssetManager assets;
    Entity prefabCreation;
    std::string editPrefabRequest, spawnPrefabRequest, editCubemapRequest, spawnModelRequest;
    std::string selected, openedScript, currentFolder;
    EditorSettings settings;
    std::filesystem::path projectFolder;
    ProjectDialog dialog;
    AudioPreview preview;
    TexturePreviewCache thumbnails;
    void importFile(const std::string& path) {
        try {
            if (std::filesystem::is_directory(path)) {
                selected = assets.import(path, currentFolder);
                std::vector<std::string> sources;
                for (const auto& entry :
                     std::filesystem::recursive_directory_iterator(assets.resolve(selected))) {
                    if (entry.is_regular_file() && importableModel(entry.path())) {
                        sources.push_back(entry.path().string());
                    }
                }
                std::string failures;
                for (const auto& source : sources) {
                    try {
                        const auto folder =
                            std::filesystem::path(source)
                                .parent_path()
                                .lexically_relative(std::filesystem::weakly_canonical(assets.root))
                                .generic_string();
                        importModel(assets, source, folder);
                    } catch (const std::exception& error) {
                        failures += std::filesystem::path(source).filename().string() + ": " +
                                    error.what() + "\n";
                    }
                }
                if (!failures.empty()) {
                    assets.error =
                        "Directory copied; some models could not be converted:\n" + failures;
                    return;
                }
            } else {
                selected = importableModel(path) ? importModel(assets, path, currentFolder)
                                                 : assets.import(path, currentFolder);
            }
            assets.error.clear();
        } catch (const std::exception& e) {
            assets.error = e.what();
        }
    }
    // Rename a file or directory and keep descendant project references intact.
    void renameSelected(World& world, const std::string& name) {
        std::filesystem::path leaf(name);
        if (!selected.empty() && !std::filesystem::is_directory(assets.resolve(selected))) {
            const auto extension = std::filesystem::path(selected).extension();
            if (leaf.extension() != extension) {
                leaf += extension.string();
            }
        }
        if (selected.empty() || leaf.empty() || leaf.has_parent_path() || leaf == ".." ||
            leaf == "." || name.find('\0') != std::string::npos ||
            name.find('\\') != std::string::npos) {
            throw std::runtime_error("Use a name without directories");
        }
        const auto relative =
            (std::filesystem::path(selected).parent_path() / leaf).generic_string();
        if (relative == selected) {
            return;
        }
        if (std::filesystem::exists(assets.resolve(relative))) {
            throw std::runtime_error("An asset with that name exists");
        }
        const bool directory = std::filesystem::is_directory(assets.resolve(selected));
        auto remap = [&](std::string& path) {
            if (path == selected) {
                path = relative;
                return true;
            }
            if (directory && path.size() > selected.size() &&
                path.compare(0, selected.size(), selected) == 0 && path[selected.size()] == '/') {
                path = relative + path.substr(selected.size());
                return true;
            }
            return false;
        };
        std::filesystem::rename(assets.resolve(selected), assets.resolve(relative));
        world.each<MeshRenderer>([&](Entity, MeshRenderer& mesh) { remap(mesh.model); });
        world.each<Collider>([&](Entity, Collider& collider) { remap(collider.model); });
        world.each<LuaScript>([&](Entity, LuaScript& s) {
            remap(s.path);
            for (auto& entry : s.additional) {
                remap(entry.path);
            }
        });
        world.each<LuaScript>([&](Entity, LuaScript& scripts) {
            auto rename = [&](std::vector<ScriptField>& fields) {
                for (auto& field : fields) {
                    if (exportAssetType(field.type)) {
                        remap(field.text);
                    }
                }
            };
            rename(scripts.fields);
            for (auto& extra : scripts.additional) {
                rename(extra.fields);
            }
        });
        world.each<PrefabInstance>([&](Entity, PrefabInstance& link) { remap(link.asset); });
        world.each<MeshRenderer>([&](Entity, MeshRenderer& r) {
            remap(r.material);
            remap(r.materialValues.texture);
            remap(r.materialValues.normalMap);
        });
        for (const auto& file : assets.files()) {
            if (modelAsset(file)) {
                try {
                    auto document = modelDocument(assets, file);
                    bool changed = false;
                    if (document.contains("settings")) {
                        auto value = document.at("settings").get<std::string>();
                        changed |= remap(value);
                        document["settings"] = value;
                    }
                    if (document.contains("parts")) {
                        for (auto& part : document["parts"]) {
                            for (const char* key : {"mesh", "material"}) {
                                auto value = part.at(key).get<std::string>();
                                changed |= remap(value);
                                part[key] = value;
                            }
                        }
                    }
                    if (changed) {
                        writeJsonFile(assets.resolve(file), document);
                    }
                } catch (const std::exception& error) {
                    assets.error = error.what();
                }
            }
            if (materialAsset(file)) {
                try {
                    auto p = loadMaterial(assets, file);
                    bool changed = false;
                    changed |= remap(p.texture);
                    changed |= remap(p.normalMap);
                    if (changed) {
                        saveMaterial(assets, file, p);
                    }
                } catch (...) {
                }
            }
        }
        world.each<AudioSource>([&](Entity, AudioSource& a) { remap(a.clip); });
        world.each<Skybox>([&](Entity, Skybox& sky) {
            for (auto* path : {&sky.right,
                               &sky.left,
                               &sky.top,
                               &sky.bottom,
                               &sky.front,
                               &sky.back,
                               &sky.hdr,
                               &sky.cubemap}) {
                remap(*path);
            }
        });
        remap(openedScript);
        remap(currentFolder);
        preview.stop();
        thumbnails.clear();
        selected = relative;
        ++modelAssetRevision;
    }
    void setRoot(const std::filesystem::path& root) {
        preview.stop();
        thumbnails.clear();
        assets.root = root;
        currentFolder.clear();
        selected.clear();
        openedScript.clear();
    }
    void draw(SDL_Window* window,
              World& world,
              Entity selectedObject,
              ScriptEditor& scripts,
              bool editable,
              EditorHistory* history = nullptr) {
        if (auto result = dialog.poll()) {
            if (!result->error.empty()) {
                assets.error = result->error;
            } else if (!result->path.empty()) {
                importFile(result->path);
            }
        }
        preview.update();
        thumbnails.beginFrame(assets);
        if (dockedEditor && !panelOpen[5]) {
            return;
        }
        if (beginEditorPanel("Assets")) {
            const bool shortcutFocus =
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
                !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && editable &&
                !dialog.pending();
            ImGui::TextDisabled(
                "%s",
                currentFolder.empty()
                    ? "Assets"
                    : std::filesystem::path(currentFolder).filename().string().c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", (assets.root / currentFolder).string().c_str());
            }
            if (!currentFolder.empty() && ImGui::ArrowButton("##UpFolder", ImGuiDir_Up)) {
                currentFolder = std::filesystem::path(currentFolder).parent_path().generic_string();
                selected.clear();
            }
            if (!currentFolder.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Up one folder");
            }
            ImGui::BeginDisabled(!editable || dialog.pending());
            ImGui::SetCursorPosX(
                std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 112));
            if (ImGui::Button("\xef\x95\xad##Import", ImVec2(32, 26))) {
                ImGui::OpenPopup("Import assets");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Import files");
            }
            if (ImGui::BeginPopup("Import assets")) {
                if (ImGui::MenuItem("Files / Models")) {
                    dialog.showImport(window);
                }
                if (ImGui::MenuItem("Directory (full structure)")) {
                    dialog.showImportDirectory(window);
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("\xef\x87\x89##NewScript", ImVec2(32, 26))) {
                createFile(".ls");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("New LegacyScript");
            }
            ImGui::SameLine();
            if (ImGui::Button("\xef\x99\x9e##NewFolder", ImVec2(32, 26))) {
                createFolder();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("New Folder");
            }
            if (!openedScript.empty()) {
                ImGui::TextWrapped("Editing asset: %s", openedScript.c_str());
                if (ImGui::Button("Save script asset")) {
                    try {
                        assets.writeScript(openedScript, scripts.source());
                    } catch (const std::exception& e) {
                        assets.error = e.what();
                    }
                }
            }
            ImGui::TextWrapped(
                "Drop files here to import. Drag LegacyScript assets onto a GameObject or Inspector to attach.");
            if (!assets.error.empty()) {
                ImGui::TextWrapped("%s", assets.error.c_str());
            }
            if (!preview.error.empty()) {
                ImGui::TextWrapped("%s", preview.error.c_str());
            }
            ImGui::BeginChild("Asset files", ImVec2(0, -32));
            try {
                std::vector<std::string> files;
                const auto folder = assets.resolve(currentFolder);
                if (std::filesystem::is_directory(folder)) {
                    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                        if (files.size() >= 10000) {
                            break;
                        }
                        if (!entry.is_symlink() &&
                            (entry.is_directory() || entry.is_regular_file())) {
                            auto path =
                                entry.path().lexically_relative(assets.root).generic_string();
                            assets.resolve(path);
                            files.push_back(path);
                        }
                    }
                }
                std::sort(files.begin(), files.end(), [&](const auto& a, const auto& b) {
                    const bool da = std::filesystem::is_directory(assets.resolve(a));
                    const bool db = std::filesystem::is_directory(assets.resolve(b));
                    return da != db ? da > db : a < b;
                });
                const int columns =
                    grid_ ? std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x /
                                                        (120 + gridSpacing_)),
                                       1,
                                       64)
                          : 1;
                if (ImGui::BeginTable("Asset items", columns, ImGuiTableFlags_SizingStretchSame)) {
                    for (const auto& file : files) {
                        const bool folderItem = std::filesystem::is_directory(assets.resolve(file));
                        const auto assetPath = std::filesystem::path(file);
                        const std::string fullName =
                            folderItem ? assetPath.filename().string() : assetPath.stem().string();
                        std::size_t end = 0;
                        int characters = 0;
                        for (; end < fullName.size() && characters < 12; ++end) {
                            if ((static_cast<unsigned char>(fullName[end]) & 0xc0) != 0x80) {
                                ++characters;
                            }
                        }
                        while (end < fullName.size() &&
                               (static_cast<unsigned char>(fullName[end]) & 0xc0) == 0x80) {
                            ++end;
                        }
                        const std::string display =
                            folderItem
                                ? fullName
                                : fullName.substr(0, end) + (end < fullName.size() ? "..." : "");
                        ImGui::TableNextColumn();
                        ImGui::PushID(file.c_str());
                        const auto iconPosition = ImGui::GetCursorScreenPos();
                        if (!grid_) {
                            ImGui::Dummy(ImVec2(36, 32));
                            ImGui::SameLine();
                        }
                        const bool clicked =
                            ImGui::Selectable("##tile",
                                              selected == file,
                                              ImGuiSelectableFlags_AllowDoubleClick |
                                                  ImGuiSelectableFlags_AllowOverlap,
                                              ImVec2(0, grid_ ? 84.0f + gridSpacing_ : 28.0f));
                        const auto itemMin = ImGui::GetItemRectMin(),
                                   itemMax = ImGui::GetItemRectMax();
                        const unsigned thumbnail = folderItem || !ImGui::IsItemVisible()
                                                       ? 0
                                                       : thumbnails.texture(assets, file);
                        if (!grid_) {
                            fileBadge(file, iconPosition, folderItem, thumbnail);
                        }
                        const bool itemHovered = ImGui::IsMouseHoveringRect(itemMin, itemMax);
                        if (itemHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) &&
                            GImGui->HoveredIdTimer >= 3.0f && ImGui::BeginTooltip()) {
                            ImGui::TextUnformatted(fullName.c_str());
                            const std::string type = folderItem                   ? "Folder"
                                                     : modelAsset(file)           ? "3D Model"
                                                     : materialAsset(file)        ? "Material"
                                                     : textureAsset(file)         ? "Texture"
                                                     : AssetManager::script(file) ? "LegacyScript"
                                                     : cubemapAsset(file)         ? "Cubemap"
                                                     : audioAsset(file)           ? "Audio"
                                                     : AssetManager::extension(file) == ".prefab"
                                                         ? "Prefab"
                                                         : AssetManager::extension(file);
                            ImGui::Text("Type: %s", type.c_str());
                            if (thumbnail) {
                                ImGui::Image(static_cast<ImTextureID>(thumbnail), ImVec2(240, 240));
                            }
                            ImGui::EndTooltip();
                        }
                        {
                            const auto a = itemMin, b = itemMax;
                            auto* draw = ImGui::GetWindowDrawList();
                            draw->PushClipRect(a, b, true);
                            if (grid_) {
                                fileBadge(file,
                                          ImVec2((a.x + b.x) * .5f - 16, a.y + 4),
                                          folderItem,
                                          thumbnail);
                            }
                            const auto text = ImGui::CalcTextSize(display.c_str());
                            draw->AddText(ImVec2(grid_ ? (a.x + b.x - text.x) * .5f
                                                       : a.x + ImGui::GetStyle().FramePadding.x,
                                                 grid_ ? a.y + 42 : (a.y + b.y - text.y) * .5f),
                                          ImGui::GetColorU32(ImGuiCol_Text),
                                          display.c_str());
                            draw->PopClipRect();
                        }
                        if (clicked) {
                            selected = file;
                            if (!folderItem && modelAsset(file) && ImGui::IsMouseDoubleClicked(0)) {
                                spawnModelRequest = file;
                            }
                            if (!folderItem && cubemapAsset(file) &&
                                ImGui::IsMouseDoubleClicked(0)) {
                                editCubemapRequest = file;
                            }
                            if (!folderItem && AssetManager::extension(file) == ".prefab" &&
                                ImGui::IsMouseDoubleClicked(0)) {
                                editPrefabRequest = file;
                            }
                            if (folderItem && ImGui::IsMouseDoubleClicked(0)) {
                                currentFolder = file;
                                selected.clear();
                            }
                            if (!folderItem && AssetManager::script(file) &&
                                ImGui::IsMouseDoubleClicked(0)) {
                                try {
                                    if (settings.external) {
                                        if (!settings.openExternal(assets.resolve(file), true)) {
                                            assets.error = settings.error;
                                        }
                                    } else {
                                        scripts.saveFile(assets);
                                        scripts.setSource(assets.readScript(file));
                                        openedScript = file;
                                        panelOpen[3] = true;
                                        ImGui::SetWindowFocus("Script Editor");
                                    }
                                } catch (const std::exception& e) {
                                    assets.error = e.what();
                                }
                            }
                        }
                        if (!folderItem && modelAsset(file) &&
                            ImGui::BeginPopupContextItem("Model actions")) {
                            if (ImGui::MenuItem("Instantiate in level")) {
                                spawnModelRequest = file;
                            }
                            ImGui::EndPopup();
                        }
                        if (!folderItem && AssetManager::extension(file) == ".prefab" &&
                            ImGui::BeginPopupContextItem("Prefab actions")) {
                            if (ImGui::MenuItem("Edit Prefab")) {
                                editPrefabRequest = file;
                            }
                            if (ImGui::MenuItem("Instantiate in level")) {
                                spawnPrefabRequest = file;
                            }
                            ImGui::EndPopup();
                        }
                        if (!folderItem && ImGui::BeginDragDropSource()) {
                            ImGui::SetDragDropPayload("ASSET_PATH", file.c_str(), file.size() + 1);
                            fileIcon(file);
                            ImGui::TextUnformatted(file.c_str());
                            ImGui::EndDragDropSource();
                        }
                        if (ImGui::BeginPopupContextItem("Asset actions")) {
                            selected = file;
                            if (!folderItem &&
                                ImGui::MenuItem("Attach LegacyScript to selected object",
                                                nullptr,
                                                false,
                                                world.alive(selectedObject) &&
                                                    AssetManager::script(file))) {
                                attachScriptAsset(world, selectedObject, file);
                            }
                            if (ImGui::MenuItem("Rename...", "F2")) {
                                selected = file;
                                renameRequested_ = true;
                            }
                            if (!folderItem && ImGui::MenuItem("Move to Trash", "Del")) {
                                selected = file;
                                deleteRequested_ = true;
                            }
                            ImGui::EndPopup();
                        }
                        if (audioAsset(file) && (itemHovered || preview.playing(file))) {
                            const auto cursor = ImGui::GetCursorScreenPos();
                            const bool active = preview.playing(file);
                            ImVec2 position(itemMax.x - 26, itemMin.y + 2);
                            ImGui::SetCursorScreenPos(position);
                            if (ImGui::InvisibleButton("##preview", ImVec2(24, 24))) {
                                preview.toggle(assets, file);
                            }
                            auto* draw = ImGui::GetWindowDrawList();
                            draw->AddRectFilled(position,
                                                ImVec2(position.x + 24, position.y + 24),
                                                IM_COL32(40, 50, 65, 240),
                                                3);
                            if (active) {
                                draw->AddRectFilled(ImVec2(position.x + 7, position.y + 7),
                                                    ImVec2(position.x + 17, position.y + 17),
                                                    IM_COL32_WHITE);
                            } else {
                                draw->AddTriangleFilled(ImVec2(position.x + 8, position.y + 5),
                                                        ImVec2(position.x + 8, position.y + 19),
                                                        ImVec2(position.x + 19, position.y + 12),
                                                        IM_COL32_WHITE);
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip(active ? "Stop preview" : "Play preview");
                            }
                            ImGui::SetCursorScreenPos(cursor);
                            // Submit an item after restoring layout: ImGui 1.92 rejects
                            // SetCursorScreenPos extending a child/table without an item.
                            ImGui::Dummy(ImVec2(0, 0));
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                // Cover the browser child, including blank space, as a hierarchy drop target.
                if (editable &&
                    ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect,
                                                     ImGui::GetID("PrefabDrop"))) {
                    if (const auto* payload = ImGui::AcceptDragDropPayload("LEVEL_OBJECT")) {
                        prefabCreation = *static_cast<const Entity*>(payload->Data);
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::BeginPopupContextWindow("Assets empty actions",
                                                   ImGuiPopupFlags_MouseButtonRight |
                                                       ImGuiPopupFlags_NoOpenOverItems)) {
                    if (ImGui::MenuItem("New Folder")) {
                        createFolder();
                    }
                    if (ImGui::BeginMenu("Create")) {
                        if (ImGui::MenuItem("Cubemap (.cubemap)")) {
                            try {
                                auto path =
                                    (std::filesystem::path(currentFolder) / "Cubemap.cubemap")
                                        .generic_string();
                                for (unsigned n = 2; std::filesystem::exists(assets.resolve(path));
                                     ++n) {
                                    path = (std::filesystem::path(currentFolder) /
                                            ("Cubemap_" + std::to_string(n) + ".cubemap"))
                                               .generic_string();
                                }
                                saveCubemap(assets, path, CubemapAsset{});
                                selected = path;
                                editCubemapRequest = path;
                            } catch (const std::exception& e) {
                                assets.error = e.what();
                            }
                        }
                        if (ImGui::MenuItem("Material (.mat)")) {
                            try {
                                selected = createMaterial(assets, currentFolder);
                            } catch (const std::exception& e) {
                                assets.error = e.what();
                            }
                        }
                        if (ImGui::MenuItem("LegacyScript (.ls)")) {
                            createFile(".ls");
                        }
                        if (ImGui::MenuItem("Text file (.txt)")) {
                            createFile(".txt");
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndPopup();
                }
                if (shortcutFocus && !selected.empty()) {
                    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                        renameRequested_ = true;
                    }
                    if (!std::filesystem::is_directory(assets.resolve(selected)) &&
                        ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                        deleteRequested_ = true;
                    }
                }
                if (renameRequested_) {
                    std::snprintf(rename_,
                                  sizeof(rename_),
                                  "%s",
                                  (std::filesystem::is_directory(assets.resolve(selected))
                                       ? std::filesystem::path(selected).filename()
                                       : std::filesystem::path(selected).stem())
                                      .string()
                                      .c_str());
                    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
                    ImGui::OpenPopup("Rename asset");
                    renameRequested_ = false;
                    renameFocus_ = true;
                }
                if (deleteRequested_) {
                    deleteRequested_ = false;
                    if (history && !selected.empty()) {
                        auto receipt = std::make_shared<TrashFile>(assets.resolve(selected));
                        if (history->perform(world, selectedObject, [receipt](bool redo) {
                                if (redo) {
                                    receipt->remove();
                                } else {
                                    receipt->restore();
                                }
                            })) {
                            if (openedScript == selected) {
                                openedScript.clear();
                                scripts.setSource("");
                                scripts.setAssetContext("", {});
                            }
                            selected.clear();
                        } else {
                            assets.error = history->error;
                        }
                    } else {
                        assets.error = "Asset deletion requires editor history";
                    }
                }
                if (ImGui::BeginPopup("Rename asset", ImGuiWindowFlags_AlwaysAutoResize)) {
                    if (renameFocus_) {
                        ImGui::SetKeyboardFocusHere();
                        renameFocus_ = false;
                    }
                    const bool renameEnter = ImGui::InputText(
                        "Name",
                        rename_,
                        sizeof(rename_),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (ImGui::Button("Rename") || renameEnter) {
                        try {
                            renameSelected(world, rename_);
                            ImGui::CloseCurrentPopup();
                        } catch (const std::exception& e) {
                            assets.error = e.what();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            } catch (const std::exception& e) {
                assets.error = e.what();
            }
            ImGui::EndChild();
            ImGui::EndDisabled();
            ImGui::SetCursorPosX(
                std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 220));
            ImGui::BeginDisabled(!grid_);
            ImGui::SetNextItemWidth(
                std::min(140.f, std::max(40.f, ImGui::GetContentRegionAvail().x - 80)));
            ImGui::SliderFloat("##GridSpacing", &gridSpacing_, 0, 64, "Spacing %.0f");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Grid spacing (grid view only)");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::SetCursorPosX(
                std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 60));
            for (int mode = 0; mode < 2; ++mode) {
                if (mode) {
                    ImGui::SameLine();
                }
                ImGui::PushID(mode);
                auto pos = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("View mode", ImVec2(26, 26))) {
                    grid_ = mode == 1;
                }
                auto* draw = ImGui::GetWindowDrawList();
                const auto color = ImGui::GetColorU32(
                    (grid_ == (mode == 1)) ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled);
                if (mode) {
                    for (int y = 0; y < 2; ++y) {
                        for (int x = 0; x < 2; ++x) {
                            draw->AddRect(ImVec2(pos.x + 5 + x * 9, pos.y + 6 + y * 9),
                                          ImVec2(pos.x + 11 + x * 9, pos.y + 11 + y * 9),
                                          color);
                        }
                    }
                } else {
                    for (int y = 0; y < 3; ++y) {
                        draw->AddLine(ImVec2(pos.x + 5, pos.y + 6 + y * 6),
                                      ImVec2(pos.x + 22, pos.y + 6 + y * 6),
                                      color,
                                      2);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(mode ? "Grid view" : "List view");
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

  private:
    void createFolder() {
        try {
            selected = assets.createFolder(currentFolder);
        } catch (const std::exception& e) {
            assets.error = e.what();
        }
    }
    void createFile(const std::string& extension) {
        try {
            const std::string base = extension == ".ls" ? "NewScript" : "NewText";
            const auto prefix = std::filesystem::path(currentFolder);
            std::string name = (prefix / (base + extension)).generic_string();
            int n = 2;
            while (std::filesystem::exists(assets.resolve(name))) {
                name = (prefix / (base + " (" + std::to_string(n++) + ")" + extension))
                           .generic_string();
            }
            if (extension == ".ls") {
                assets.writeScript(name, "");
            } else {
                std::filesystem::create_directories(assets.root);
                std::ofstream out(assets.resolve(name));
                if (!out) {
                    throw std::runtime_error("Cannot create text asset");
                }
            }
            selected = name;
        } catch (const std::exception& e) {
            assets.error = e.what();
        }
    }
    char rename_[256]{};
    float gridSpacing_ = 0;
    bool renameRequested_ = false, deleteRequested_ = false, grid_ = false, renameFocus_ = false;
};
