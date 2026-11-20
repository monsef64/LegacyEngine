#pragma once
#include "animation/animator_sheet.hpp"
#include "assets/assets.hpp"
#include "assets/cubemap.hpp"
#include "assets/texture_preview.hpp"
#include "audio/audio_preview.hpp"
#include "editor/asset_icons.hpp"
#include "editor/asset_preview_ui.hpp"
#include "editor/editor_history.hpp"
#include "editor/editor_layout.hpp"
#include "editor/editor_settings.hpp"
#include "editor/script_editor.hpp"
#include "materials/material_io.hpp"
#include "models/model_asset.hpp"
#include "platform/open_location.hpp"
#include "platform/project_dialog.hpp"
#include "platform/system_trash.hpp"
#include "project/project_assets.hpp"
#include <deque>
#include <future>
#include <imgui_internal.h>
#include <set>

class AssetBrowser {
    UntitledProjectAssets staging_;
    struct ModelGroup {
        std::filesystem::file_time_type stamp{};
        unsigned revision = ~0u;
        std::vector<std::string> children;
        std::map<std::string, std::string> names;
    };
    std::map<std::string, ModelGroup> modelGroups_;
    std::set<std::string> expandedModels_;

    // UI snapshots only: all actual file operations still use AssetManager::resolve.
    // Never canonicalize/stat paths from a sort comparator or every drawn tile.
    std::filesystem::path listingRoot_;
    std::string listingFolder_;
    bool listingRecursive_ = false;
    unsigned listingRevision_ = ~0u;
    double listingUntil_ = 0, selectionCheckAt_ = 0;
    std::vector<std::string> listingFiles_;
    std::map<std::string, bool> listingDirectories_;

    std::string transientError_;
    double errorUntil_ = 0;
    unsigned thumbnailRevision_ = ~0u;

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
    std::string editPrefabRequest, spawnPrefabRequest, editCubemapRequest, spawnModelRequest,
        editAnimationRequest, previewModelRequest;
    std::string selected, openedScript, currentFolder;
    std::pair<std::string, std::string> movedAsset;
    EditorSettings settings;
    std::filesystem::path projectFolder;
    ProjectDialog dialog;
    ImGuiTextFilter search;
    AudioPreview preview;
    TexturePreviewCache thumbnails;
    struct ImportResult {
        std::filesystem::path root;
        std::string selected, error;
    };
    struct ImportRequest {
        AssetManager manager;
        std::string folder, path;
    };
    std::future<ImportResult> importJob_;
    std::deque<ImportRequest> importQueue_;
    std::string importErrors_, activeImportPath_;
    bool importing() const {
        return importJob_.valid() || !importQueue_.empty();
    }
    void startNextImport() {
        if (importJob_.valid() || importQueue_.empty()) {
            return;
        }
        auto request = std::move(importQueue_.front());
        importQueue_.pop_front();
        activeImportPath_ = request.path;
        importJob_ = std::async(std::launch::async, [request = std::move(request)]() mutable {
            return runImport(std::move(request.manager), std::move(request.folder), request.path);
        });
    }
    void pollImport() {
        if (importJob_.valid() &&
            importJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = importJob_.get();
                ++modelAssetRevision;
                if (result.root == assets.root) {
                    if (!result.selected.empty()) {
                        selected = result.selected;
                        expandedModels_.insert(selected);
                    }
                    if (!result.error.empty()) {
                        importErrors_ +=
                            std::filesystem::path(activeImportPath_).filename().string() + ": " +
                            result.error + "\n";
                    }
                    assets.error = importErrors_;
                    thumbnails.clear();
                }
            } catch (const std::exception& e) {
                importErrors_ += std::string("Import failed: ") + e.what() + "\n";
                assets.error = importErrors_;
            }
        }
        startNextImport();
    }
    void importFile(const std::string& path) {
        if (path.empty()) {
            return;
        }
        if (!importing()) {
            importErrors_.clear();
            assets.error.clear();
        }
        // Own each SDL drop path immediately; later events can invalidate its pointer.
        // Capture root/folder now so navigation cannot redirect queued imports.
        importQueue_.push_back({assets, currentFolder, path});
        startNextImport();
    }
    static ImportResult
    runImport(AssetManager assets, std::string currentFolder, const std::string& path) {
        std::string selected;
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
                        importModel(assets, source, folder, false);
                    } catch (const std::exception& error) {
                        failures += std::filesystem::path(source).filename().string() + ": " +
                                    error.what() + "\n";
                    }
                }
                if (!failures.empty()) {
                    assets.error =
                        "Directory copied; some models could not be converted:\n" + failures;
                    return {assets.root, selected, assets.error};
                }
            } else {
                selected = importableModel(path) ? importModel(assets, path, currentFolder, false)
                                                 : assets.import(path, currentFolder);
            }
            assets.error.clear();
        } catch (const std::exception& e) {
            assets.error = e.what();
        }
        return {assets.root, selected, assets.error};
    }
    // Rename a file or directory and keep descendant project references intact.
    void renameSelected(World& world, const std::string& name, bool fullPath = false) {
        std::filesystem::path leaf(name);
        if (!fullPath && !selected.empty() &&
            !std::filesystem::is_directory(assets.resolve(selected))) {
            const auto extension = std::filesystem::path(selected).extension();
            if (leaf.extension() != extension) {
                leaf += extension.string();
            }
        }
        if (selected.empty() || leaf.empty() || (!fullPath && leaf.has_parent_path()) ||
            leaf == ".." || leaf == "." || name.find('\0') != std::string::npos ||
            name.find('\\') != std::string::npos) {
            throw std::runtime_error("Use a name without directories");
        }
        const auto relative =
            fullPath ? std::filesystem::path(name).lexically_normal().generic_string()
                     : (std::filesystem::path(selected).parent_path() / leaf).generic_string();
        if (relative == selected) {
            return;
        }
        if (std::filesystem::exists(assets.resolve(relative))) {
            throw std::runtime_error("An asset with that name exists");
        }
        const bool directory = std::filesystem::is_directory(assets.resolve(selected));
        const auto sourcePath = assets.resolve(selected),
                   destinationPath = assets.resolve(relative);
        if (directory) {
            auto inside = destinationPath.lexically_relative(sourcePath);
            if (inside.empty() || *inside.begin() != "..") {
                throw std::runtime_error("Cannot move a folder into itself");
            }
        }
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
        world.each<MeshRenderer>([&](Entity, MeshRenderer& mesh) {
            remap(mesh.model);
            remap(mesh.lightmap);
        });
        world.each<Terrain>([&](Entity, Terrain& t) {
            if (remap(t.asset)) {
                t.loaded = assets.resolve(t.asset).generic_string();
            }
            if (t.data) {
                bool copy = false;
                for (const auto& layer : t.data->layers) {
                    auto path = layer.texture;
                    copy |= remap(path);
                    path = layer.normalMap;
                    copy |= remap(path);
                    path = layer.material;
                    copy |= remap(path);
                }
                auto atlas = t.data->lightmap;
                copy |= remap(atlas);
                if (copy) {
                    if (!t.data.unique()) {
                        t.data = std::make_shared<Terrains::Data>(*t.data);
                    }
                    for (auto& layer : t.data->layers) {
                        remap(layer.texture);
                        remap(layer.normalMap);
                        remap(layer.material);
                    }
                    remap(t.data->lightmap);
                    t.data->revision = Terrains::nextRevision();
                }
            }
        });
        world.each<Collider>([&](Entity, Collider& collider) { remap(collider.model); });
        world.each<LightShafts>([&](Entity, LightShafts& s) { remap(s.texture); });
        auto remapLight = [&](Entity, auto& l) {
            remap(l.cookie);
            remap(l.bake);
        };
        world.each<DirectionalLight>(remapLight);
        world.each<PointLight>(remapLight);
        world.each<SpotLight>(remapLight);
        world.each<ParticleSystem>([&](Entity, ParticleSystem& p) { remap(p.texture); });
        world.each<Animation>([&](Entity, Animation& a) {
            remap(a.clip);
            remap(a.model);
        });
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
            const auto extension = AssetManager::extension(file);
            if (extension == ".leanim" || extension == ".prefab" || extension == ".level" ||
                extension == ".cubemap" || extension == ".terrain") {
                try {
                    auto doc = readJsonFile(assets.resolve(file), 256 * 1024 * 1024);
                    bool changed = false;
                    auto update = [&](auto&& self, Json& value, const std::string& key) -> void {
                        if (value.is_object()) {
                            for (auto it = value.begin(); it != value.end(); ++it) {
                                self(self, it.value(), it.key());
                            }
                        } else if (value.is_array()) {
                            for (auto& child : value) {
                                self(self, child, key);
                            }
                        } else if (value.is_string()) {
                            const std::string keys =
                                "|subAssets|dataFolder|mesh|settings|material|texture|normalMap|cookie|bake|model|lightmap|clip|asset|path|hdr|cubemap|right|left|top|bottom|front|back|";
                            if (keys.find("|" + key + "|") == std::string::npos) {
                                return;
                            }
                            auto path = value.get<std::string>();
                            if (remap(path)) {
                                value = path;
                                changed = true;
                            }
                        }
                    };
                    update(update, doc, "");
                    if (changed) {
                        writeJsonFile(assets.resolve(file), doc);
                    }
                } catch (const std::exception& e) {
                    assets.error = e.what();
                }
            }
            if (modelAsset(file)) {
                try {
                    auto document = modelDocument(assets, file);
                    bool changed = false;
                    if (document.contains("subAssetNames") &&
                        document["subAssetNames"].is_object()) {
                        Json names = Json::object();
                        for (auto it = document["subAssetNames"].begin();
                             it != document["subAssetNames"].end();
                             ++it) {
                            auto path = it.key();
                            changed |= remap(path);
                            names[path] = it.value();
                        }
                        document["subAssetNames"] = std::move(names);
                    }
                    if (document.contains("subAssets")) {
                        for (auto& entry : document["subAssets"]) {
                            auto value = entry.get<std::string>();
                            changed |= remap(value);
                            entry = value;
                        }
                    }
                    if (document.contains("dataFolder")) {
                        auto value = document.at("dataFolder").get<std::string>();
                        changed |= remap(value);
                        document["dataFolder"] = value;
                    }
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
        movedAsset = {selected, relative};
        selected = relative;
        ++modelAssetRevision;
    }
    std::string clipboardPath_, pasteFolder_;
    std::filesystem::path clipboardRoot_;
    bool clipboardCut_ = false, pasteRequested_ = false;
    void copySelected(bool cut) {
        if (selected.empty()) {
            return;
        }
        clipboardPath_ = selected;
        clipboardRoot_ = assets.root;
        clipboardCut_ = cut;
    }
    void pasteAssets(World& world, const std::string& folder) {
        if (clipboardPath_.empty() || clipboardRoot_ != assets.root) {
            throw std::runtime_error("Copy an asset from this project first");
        }
        const auto source = assets.resolve(clipboardPath_);
        const bool directory = std::filesystem::is_directory(source);
        auto leaf = source.filename();
        std::string target = (std::filesystem::path(folder) / leaf).generic_string();
        auto inside = assets.resolve(folder).lexically_relative(source);
        if (directory && (inside.empty() || *inside.begin() != "..")) {
            throw std::runtime_error("Cannot paste a folder inside itself");
        }
        for (unsigned n = 2; std::filesystem::exists(assets.resolve(target)); ++n) {
            target = (std::filesystem::path(folder) /
                      ((directory ? leaf.string() : leaf.stem().string()) + " (" +
                       std::to_string(n) + ")" + (directory ? "" : leaf.extension().string())))
                         .generic_string();
        }
        const auto old = clipboardPath_;
        if (clipboardCut_) {
            selected = old;
            renameSelected(world, target, true);
            clipboardPath_.clear();
        } else {
            const auto copied = assets.import(source, folder, true);
            // Only references inside the copied tree are changed. The original
            // hierarchy and all existing scene instances keep their own assets.
            const auto destination = assets.resolve(copied);
            auto remap = [&](auto&& self, Json& value, const std::string& key) -> void {
                if (key == "subAssetNames" && value.is_object()) {
                    Json names = Json::object();
                    for (auto it = value.begin(); it != value.end(); ++it) {
                        auto path = it.key();
                        if (path == old || (directory && path.rfind(old + "/", 0) == 0)) {
                            path = copied + path.substr(old.size());
                        }
                        names[path] = it.value();
                    }
                    value = std::move(names);
                } else if (value.is_object()) {
                    for (auto it = value.begin(); it != value.end(); ++it) {
                        self(self, it.value(), it.key());
                    }
                } else if (value.is_array()) {
                    for (auto& child : value) {
                        self(self, child, key);
                    }
                } else if (value.is_string()) {
                    const std::string keys =
                        "|subAssets|dataFolder|mesh|settings|material|texture|normalMap|cookie|bake|model|lightmap|clip|asset|path|hdr|cubemap|right|left|top|bottom|front|back|";
                    if (keys.find("|" + key + "|") == std::string::npos) {
                        return;
                    }
                    auto p = value.get<std::string>();
                    if (p == old || (directory && p.size() > old.size() &&
                                     p.compare(0, old.size(), old) == 0 && p[old.size()] == '/')) {
                        value = copied + p.substr(old.size());
                    }
                }
            };
            auto fix = [&](const std::filesystem::path& file) {
                const auto ext = AssetManager::extension(file);
                if (ext != ".mesh" && ext != ".lemesh" && ext != ".leanim" && ext != ".mat" &&
                    ext != ".prefab" && ext != ".level" && ext != ".cubemap" && ext != ".terrain") {
                    return;
                }
                auto doc = readJsonFile(file, 256 * 1024 * 1024);
                remap(remap, doc, "");
                writeJsonFile(file, doc);
            };
            try {
                if (directory) {
                    for (const auto& entry :
                         std::filesystem::recursive_directory_iterator(destination)) {
                        if (entry.is_regular_file()) {
                            fix(entry.path());
                        }
                    }
                } else {
                    fix(destination);
                }
            } catch (...) {
                std::error_code ec;
                std::filesystem::remove_all(destination, ec);
                throw;
            }
            selected = copied;
            ++modelAssetRevision;
        }
        thumbnails.clear();
        assets.error.clear();
    }
    void setRoot(const std::filesystem::path& root) {
        // An in-flight worker owns its old manager and may finish there. Never
        // redirect its remaining queue into a newly opened project.
        importQueue_.clear();
        importErrors_.clear();
        preview.stop();
        thumbnails.clear();
        clipboardPath_.clear();
        modelGroups_.clear();
        expandedModels_.clear();
        transientError_.clear();
        errorUntil_ = 0;
        assets.root = root;
        listingUntil_ = selectionCheckAt_ = 0;
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
        pollImport();
        if (auto result = dialog.poll()) {
            if (!result->error.empty()) {
                assets.error = result->error;
            } else if (!result->paths.empty()) {
                for (const auto& path : result->paths) {
                    importFile(path);
                }
            } else if (!result->path.empty()) {
                importFile(result->path);
            }
        }
        preview.update();
        if (thumbnailRevision_ != modelAssetRevision) {
            thumbnails.clear();
            thumbnailRevision_ = modelAssetRevision;
        }
        if (ImGui::GetTime() >= selectionCheckAt_) {
            selectionCheckAt_ = ImGui::GetTime() + 1.0;
            if (!selected.empty() && !std::filesystem::exists(assets.resolve(selected))) {
                selected.clear();
            }
        }
        thumbnails.beginFrame(assets);
        if (dockedEditor && !panelOpen[5]) {
            return;
        }
        if (beginEditorPanel("Assets")) {
            if (importing()) {
                ImGui::Text("Importing assets: %zu waiting", importQueue_.size());
                ImGui::TextWrapped("%s", activeImportPath_.c_str());
                ImGui::TextDisabled(
                    "One worker is converting and compressing meshes. Asset edits are paused.");
                ImGui::End();
                return;
            }

            const bool shortcutFocus =
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::GetIO().WantTextInput &&
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
            ImGui::SameLine();
            if (!currentFolder.empty() && ImGui::ArrowButton("##UpFolder", ImGuiDir_Up)) {
                currentFolder = std::filesystem::path(currentFolder).parent_path().generic_string();
                selected.clear();
            }
            if (!currentFolder.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Up one folder");
            }
            if (!currentFolder.empty()) {
                ImGui::SameLine();
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
            ImGui::EndDisabled();
            if (!assets.error.empty() || !preview.error.empty()) {
                transientError_ = assets.error.empty() ? preview.error : assets.error;
                assets.error.clear();
                preview.error.clear();
                errorUntil_ = ImGui::GetTime() + 5;
            }
            search.Draw("Search assets", -1);
            ImGui::BeginChild("Asset files", ImVec2(0, -32));
            try {
                const bool rootChanged = listingRoot_ != assets.root;
                const bool refreshListing = rootChanged || listingFolder_ != currentFolder ||
                                            listingRecursive_ != search.IsActive() ||
                                            listingRevision_ != modelAssetRevision ||
                                            ImGui::GetTime() >= listingUntil_;
                if (refreshListing) {
                    if (rootChanged) {
                        modelGroups_.clear();
                        expandedModels_.clear();
                    }
                    listingUntil_ = ImGui::GetTime() + 1.0;
                    std::vector<std::string> nextFiles;
                    std::map<std::string, bool> nextDirectories;
                    if (!currentFolder.empty() &&
                        !std::filesystem::is_directory(assets.resolve(currentFolder))) {
                        currentFolder.clear();
                    }
                    const auto folder = assets.resolve(currentFolder);
                    if (search.IsActive()) {
                        for (const auto& path : assets.files()) {
                            if (currentFolder.empty() || path.rfind(currentFolder + "/", 0) == 0) {
                                nextFiles.push_back(path);
                                nextDirectories[path] = false;
                            }
                        }
                    } else if (std::filesystem::is_directory(folder)) {
                        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                            if (nextFiles.size() >= 10000) {
                                break;
                            }
                            if (entry.is_symlink()) {
                                continue;
                            }
                            const bool directory = entry.is_directory();
                            if (directory || entry.is_regular_file()) {
                                auto path =
                                    entry.path().lexically_relative(folder).generic_string();
                                path =
                                    (std::filesystem::path(currentFolder) / path).generic_string();
                                assets.resolve(path); // validate once while collecting the snapshot
                                nextDirectories[path] = directory;
                                nextFiles.push_back(std::move(path));
                            }
                        }
                    }
                    std::sort(
                        nextFiles.begin(), nextFiles.end(), [&](const auto& a, const auto& b) {
                            const bool da = nextDirectories.at(a), db = nextDirectories.at(b);
                            return da != db ? da > db : a < b;
                        });
                    listingFiles_ = std::move(nextFiles);
                    listingDirectories_ = std::move(nextDirectories);
                    listingRoot_ = assets.root;
                    listingFolder_ = currentFolder;
                    listingRecursive_ = search.IsActive();
                    listingRevision_ = modelAssetRevision;
                }
                auto files = listingFiles_;
                std::map<std::string, std::string> childOwners, childNames;
                // Root model names sort before their namespaced child files.
                // Cache only the small manifest, not the decoded geometry.
                for (const auto& file : files) {
                    if (!modelAsset(file) || childOwners.count(file)) {
                        continue;
                    }
                    auto& group = modelGroups_[file];
                    try {
                        const auto stamp =
                            refreshListing || group.revision != modelAssetRevision
                                ? std::filesystem::last_write_time(assets.resolve(file))
                                : group.stamp;
                        if (group.revision != modelAssetRevision || group.stamp != stamp) {
                            group = {};
                            group.stamp = stamp;
                            group.revision = modelAssetRevision;
                            const auto doc = modelDocument(assets, file);
                            if (doc.contains("subAssets") && doc["subAssets"].is_array() &&
                                doc["subAssets"].size() <= 10000) {
                                const auto names = doc.value("subAssetNames", Json::object());
                                for (const auto& entry : doc["subAssets"]) {
                                    const auto child = entry.get<std::string>();
                                    if (child != file &&
                                        std::filesystem::path(child).parent_path() ==
                                            std::filesystem::path(file).parent_path()) {
                                        assets.resolve(child);
                                        group.children.push_back(child);
                                        group.names[child] = names.value(
                                            child, std::filesystem::path(child).stem().string());
                                    }
                                }
                            }
                        }
                        for (const auto& child : group.children) {
                            childOwners[child] = file;
                            childNames[child] = group.names[child];
                        }
                    } catch (
                        const std::exception&) { /* Invalid files remain selectable/deletable. */
                    }
                }
                std::vector<std::string> grouped;
                for (const auto& file : files) {
                    if (childOwners.count(file)) {
                        continue;
                    }
                    grouped.push_back(file);
                    const auto group = modelGroups_.find(file);
                    if (group != modelGroups_.end() &&
                        (expandedModels_.count(file) || search.IsActive())) {
                        for (const auto& child : group->second.children) {
                            if (childOwners[child] == file && listingDirectories_.count(child) &&
                                !listingDirectories_.at(child)) {
                                grouped.push_back(child);
                            }
                        }
                    }
                }
                files = std::move(grouped);
                const int columns =
                    grid_ ? std::clamp(
                                static_cast<int>(ImGui::GetContentRegionAvail().x /
                                                 (std::max(100.f, iconSize_ + 40) + gridSpacing_)),
                                1,
                                64)
                          : 1;
                if (ImGui::BeginTable("Asset items", columns, ImGuiTableFlags_SizingStretchSame)) {
                    for (const auto& file : files) {
                        const bool folderItem = listingDirectories_.at(file);
                        const auto assetPath = std::filesystem::path(file);
                        const std::string fullName = childNames.count(file) ? childNames.at(file)
                                                     : folderItem ? assetPath.filename().string()
                                                                  : assetPath.stem().string();
                        if (!search.PassFilter(file.c_str()) &&
                            !search.PassFilter(fullName.c_str())) {
                            continue;
                        }
                        const bool expandable =
                            modelGroups_.count(file) && !modelGroups_.at(file).children.empty();
                        const bool childItem = childOwners.count(file) != 0;
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
                        if (!grid_ && childItem) {
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18);
                        }
                        const auto iconPosition = ImGui::GetCursorScreenPos();
                        if (!grid_) {
                            ImGui::Dummy(ImVec2(iconSize_ + 4, iconSize_));
                            ImGui::SameLine();
                        }
                        const bool clicked = ImGui::Selectable(
                            "##tile",
                            selected == file,
                            ImGuiSelectableFlags_AllowDoubleClick |
                                ImGuiSelectableFlags_AllowOverlap,
                            ImVec2(0,
                                   grid_ ? iconSize_ + ImGui::GetFontSize() + 24 + gridSpacing_
                                         : iconSize_ + 4));
                        const auto itemMin = ImGui::GetItemRectMin(),
                                   itemMax = ImGui::GetItemRectMax();
                        unsigned thumbnail = folderItem || !ImGui::IsItemVisible()
                                                 ? 0
                                                 : thumbnails.texture(assets, file);
                        if (!folderItem && modelAsset(file) && ImGui::IsItemVisible() &&
                            assetPreview) {
                            thumbnail = assetPreview(assets, file, 2).texture;
                        }
                        if (!grid_) {
                            fileBadge(file, iconPosition, folderItem, thumbnail, iconSize_);
                        }
                        const bool itemHovered = ImGui::IsMouseHoveringRect(itemMin, itemMax);
                        if (itemHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) &&
                            GImGui->HoveredIdTimer >= 1.5f && ImGui::BeginTooltip()) {
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
                            if (!folderItem) {
                                drawAssetPreview(
                                    assets,
                                    file,
                                    std::min(384.f, ImGui::GetMainViewport()->WorkSize.x * .65f),
                                    0);
                            }
                            ImGui::EndTooltip();
                        }
                        {
                            const auto a = itemMin, b = itemMax;
                            auto* draw = ImGui::GetWindowDrawList();
                            draw->PushClipRect(a, b, true);
                            if (childItem) {
                                draw->AddRect(a, b, IM_COL32(95, 125, 150, 120), 4);
                            }
                            if (grid_) {
                                fileBadge(file,
                                          ImVec2((a.x + b.x - iconSize_) * .5f, a.y + 4),
                                          folderItem,
                                          thumbnail,
                                          iconSize_);
                            }
                            const auto text = ImGui::CalcTextSize(display.c_str());
                            draw->AddText(
                                ImVec2(grid_ ? (a.x + b.x - text.x) * .5f
                                             : a.x + ImGui::GetStyle().FramePadding.x,
                                       grid_ ? a.y + iconSize_ + 10 : (a.y + b.y - text.y) * .5f),
                                ImGui::GetColorU32(ImGuiCol_Text),
                                display.c_str());
                            draw->PopClipRect();
                        }
                        bool toggleGroup = false;
                        if (expandable) {
                            const ImVec2 center{itemMax.x - 10, itemMin.y + 12};
                            auto* draw = ImGui::GetWindowDrawList();
                            if (expandedModels_.count(file)) {
                                draw->AddTriangleFilled({center.x - 5, center.y - 3},
                                                        {center.x + 5, center.y - 3},
                                                        {center.x, center.y + 4},
                                                        IM_COL32_WHITE);
                            } else {
                                draw->AddTriangleFilled({center.x - 3, center.y - 5},
                                                        {center.x - 3, center.y + 5},
                                                        {center.x + 4, center.y},
                                                        IM_COL32_WHITE);
                            }
                            const auto mouse = ImGui::GetIO().MousePos;
                            toggleGroup =
                                clicked && mouse.x >= itemMax.x - 24 && mouse.y < itemMin.y + 26;
                            if (toggleGroup) {
                                if (!expandedModels_.erase(file)) {
                                    expandedModels_.insert(file);
                                }
                            }
                        }
                        if (clicked && !toggleGroup) {
                            selected = file;
                            if (!folderItem && AssetManager::extension(file) == ".leanim" &&
                                ImGui::IsMouseDoubleClicked(0)) {
                                editAnimationRequest = file;
                            }
                            if (!folderItem && modelAsset(file) && ImGui::IsMouseDoubleClicked(0)) {
                                previewModelRequest = file;
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
                                        if (!settings.openScript(projectFolder,
                                                                 assets.resolve(file))) {
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
                            selected = file;
                            if (ImGui::MenuItem(folderItem ? "Open folder location"
                                                           : "Open asset location")) {
                                try {
                                    openLocation(assets.resolve(file));
                                } catch (const std::exception& e) {
                                    assets.error = e.what();
                                }
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Instantiate in level")) {
                                spawnModelRequest = file;
                            }
                            ImGui::EndPopup();
                        }
                        if (!folderItem && AssetManager::extension(file) == ".prefab" &&
                            ImGui::BeginPopupContextItem("Prefab actions")) {
                            selected = file;
                            if (ImGui::MenuItem(folderItem ? "Open folder location"
                                                           : "Open asset location")) {
                                try {
                                    openLocation(assets.resolve(file));
                                } catch (const std::exception& e) {
                                    assets.error = e.what();
                                }
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Edit Prefab", nullptr, false, editable)) {
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
                        if (editable && ImGui::BeginPopupContextItem("Asset actions")) {
                            selected = file;
                            if (ImGui::MenuItem(folderItem ? "Open folder location"
                                                           : "Open asset location")) {
                                try {
                                    openLocation(assets.resolve(file));
                                } catch (const std::exception& e) {
                                    assets.error = e.what();
                                }
                            }
                            ImGui::Separator();
                            if (!folderItem && AssetManager::extension(file) == ".leanim" &&
                                ImGui::IsMouseDoubleClicked(0)) {
                                editAnimationRequest = file;
                            }
                            if (!folderItem &&
                                ImGui::MenuItem("Attach LegacyScript to selected object",
                                                nullptr,
                                                false,
                                                world.alive(selectedObject) &&
                                                    AssetManager::script(file))) {
                                attachScriptAsset(world, selectedObject, file);
                            }
                            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                                copySelected(false);
                            }
                            if (ImGui::MenuItem("Move (Cut)", "Ctrl+X")) {
                                copySelected(true);
                            }
                            if (folderItem && ImGui::MenuItem("Paste into folder",
                                                              "Ctrl+V",
                                                              false,
                                                              !clipboardPath_.empty())) {
                                pasteFolder_ = file;
                                pasteRequested_ = true;
                            }
                            if (ImGui::MenuItem("Rename...", "F2")) {
                                selected = file;
                                if (!folderItem && AssetManager::extension(file) == ".leanim" &&
                                    ImGui::IsMouseDoubleClicked(0)) {
                                    editAnimationRequest = file;
                                }
                                renameRequested_ = true;
                            }
                            if (ImGui::MenuItem("Move to Trash", "Del")) {
                                selected = file;
                                if (!folderItem && AssetManager::extension(file) == ".leanim" &&
                                    ImGui::IsMouseDoubleClicked(0)) {
                                    editAnimationRequest = file;
                                }
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
                if (editable &&
                    ImGui::BeginPopupContextWindow("Assets empty actions",
                                                   ImGuiPopupFlags_MouseButtonRight |
                                                       ImGuiPopupFlags_NoOpenOverItems)) {
                    if (ImGui::MenuItem("Paste", "Ctrl+V", false, !clipboardPath_.empty())) {
                        pasteFolder_ = currentFolder;
                        pasteRequested_ = true;
                    }
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
                                listingUntil_ = 0;
                                selected = path;
                                editCubemapRequest = path;
                            } catch (const std::exception& e) {
                                assets.error = e.what();
                            }
                        }
                        if (ImGui::MenuItem("Material (.mat)")) {
                            try {
                                selected = createMaterial(assets, currentFolder);
                                listingUntil_ = 0;
                            } catch (const std::exception& e) {
                                assets.error = e.what();
                            }
                        }
                        if (ImGui::MenuItem("AnimatorSheet (.animatorsheet)")) {
                            try {
                                selected = Animators::create(assets, currentFolder);
                                listingUntil_ = 0;
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
                    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                        copySelected(false);
                    }
                    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
                        copySelected(true);
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                        deleteRequested_ = true;
                    }
                }
                if (shortcutFocus && ImGui::GetIO().KeyCtrl &&
                    ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                    pasteFolder_ = currentFolder;
                    pasteRequested_ = true;
                }
                if (!editable) {
                    pasteRequested_ = renameRequested_ = deleteRequested_ = false;
                }
                if (pasteRequested_) {
                    pasteRequested_ = false;
                    try {
                        pasteAssets(world, pasteFolder_);
                    } catch (const std::exception& e) {
                        assets.error = e.what();
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
                        auto receipts = std::make_shared<std::vector<TrashFile>>();
                        receipts->emplace_back(assets.resolve(selected));
                        if (modelAsset(selected)) {
                            try {
                                const auto doc = modelDocument(assets, selected);
                                if (doc.contains("subAssets") && doc["subAssets"].is_array() &&
                                    doc["subAssets"].size() <= 10000) {
                                    std::set<std::string> included;
                                    for (const auto& value : doc["subAssets"]) {
                                        const auto child = value.get<std::string>();
                                        if (child != selected && included.insert(child).second &&
                                            std::filesystem::path(child).parent_path() ==
                                                std::filesystem::path(selected).parent_path() &&
                                            std::filesystem::path(child).filename().string().find(
                                                "__") != std::string::npos &&
                                            std::filesystem::is_regular_file(
                                                assets.resolve(child))) {
                                            receipts->emplace_back(assets.resolve(child));
                                        }
                                    }
                                }
                                std::string owned = doc.value("dataFolder", std::string{});
                                if (owned.empty() && doc.value("version", 0) == 2 &&
                                    doc.contains("parts") && !doc["parts"].empty()) {
                                    const auto part = std::filesystem::path(
                                        doc["parts"][0].at("mesh").get<std::string>());
                                    const auto candidate = part.parent_path();
                                    const auto parent =
                                        std::filesystem::path(selected).parent_path();
                                    if (candidate.parent_path() == parent &&
                                        candidate.filename().string().rfind(
                                            std::filesystem::path(selected).stem().string() +
                                                "_data",
                                            0) == 0) {
                                        owned = candidate.generic_string();
                                    }
                                }
                                if (!owned.empty() &&
                                    std::filesystem::is_directory(assets.resolve(owned)) &&
                                    std::filesystem::path(owned).parent_path() ==
                                        std::filesystem::path(selected).parent_path() &&
                                    !std::filesystem::path(owned).filename().empty() &&
                                    owned != selected) {
                                    // All generated part meshes must belong to this directory;
                                    // never remove an arbitrary directory named in a damaged file.
                                    bool ownsParts = doc.contains("parts") &&
                                                     doc["parts"].is_array() &&
                                                     !doc["parts"].empty();
                                    if (ownsParts) {
                                        for (const auto& part : doc["parts"]) {
                                            const auto mesh = part.at("mesh").get<std::string>();
                                            ownsParts &= mesh.rfind(owned + "/", 0) == 0;
                                        }
                                    }
                                    if (ownsParts) {
                                        receipts->emplace_back(assets.resolve(owned));
                                    }
                                }
                            } catch (const std::exception&) { /* A damaged model itself can still be
                                                                 deleted. */
                            }
                        }
                        if (history->performAsset(world, selectedObject, [receipts](bool redo) {
                                if (redo) {
                                    std::size_t done = 0;
                                    try {
                                        for (auto& receipt : *receipts) {
                                            receipt.remove();
                                            ++done;
                                        }
                                    } catch (...) {
                                        while (done) {
                                            (*receipts)[--done].restore();
                                        }
                                        throw;
                                    }
                                } else {
                                    std::size_t done = 0;
                                    try {
                                        for (auto it = receipts->rbegin(); it != receipts->rend();
                                             ++it) {
                                            it->restore();
                                            ++done;
                                        }
                                    } catch (...) {
                                        for (std::size_t i = receipts->size() - done;
                                             i < receipts->size();
                                             ++i) {
                                            (*receipts)[i].remove();
                                        }
                                        throw;
                                    }
                                }
                                ++modelAssetRevision;
                            })) {
                            if (openedScript == selected ||
                                (openedScript.size() > selected.size() &&
                                 openedScript.compare(0, selected.size(), selected) == 0 &&
                                 openedScript[selected.size()] == '/')) {
                                openedScript.clear();
                                scripts.setSource("");
                                scripts.setAssetContext("", {});
                            }
                            selected.clear();
                            preview.stop();
                            thumbnails.clear();
                        } else {
                            assets.error = history->error;
                        }
                    } else {
                        assets.error = "Asset deletion requires editor history";
                    }
                }
                if (editable &&
                    ImGui::BeginPopup("Rename asset", ImGuiWindowFlags_AlwaysAutoResize)) {
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
            ImGui::SetCursorPosX(
                std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 360));
            ImGui::SetNextItemWidth(std::max(45.f, (ImGui::GetContentRegionAvail().x - 80) * .5f));
            ImGui::SliderFloat("##IconSize", &iconSize_, 20, 96, "Icons %.0f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Asset icon size (grid and list)");
            }
            ImGui::SameLine();
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
            if (ImGui::GetTime() < errorUntil_ &&
                ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32);
                ImGui::TextColored(ImVec4(1, .25f, .25f, 1), "%s", transientError_.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        ImGui::End();
    }

  private:
    void createFolder() {
        try {
            selected = assets.createFolder(currentFolder);
            listingUntil_ = 0;
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
            listingUntil_ = 0;
        } catch (const std::exception& e) {
            assets.error = e.what();
        }
    }
    char rename_[256]{};
    float gridSpacing_ = 0, iconSize_ = 60;
    bool renameRequested_ = false, deleteRequested_ = false, grid_ = true, renameFocus_ = false;
};
