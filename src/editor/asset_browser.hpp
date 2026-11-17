#pragma once
#include "assets/assets.hpp"
#include "assets/cubemap.hpp"
#include "assets/texture_preview.hpp"
#include "audio/audio_preview.hpp"
#include "editor/asset_preview_ui.hpp"
#include "editor/editor_history.hpp"
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
    void setCurrentFolder(const std::string& folder) {
        currentFolder = folder;
    }
    bool deleteSelected(bool confirmTrash = true) {
        (void)confirmTrash;
        if (selected.empty()) {
            return false;
        }
        try {
            const auto path = assets.resolve(selected);
            if (assets.root.empty() || !std::filesystem::exists(path)) {
                return false;
            }
            // A directory in use by the scene is refused, as before.
            std::filesystem::remove_all(path);
            thumbnails.clear();
            selected.clear();
            ++modelAssetRevision;
            return true;
        } catch (const std::exception& e) {
            assets.error = e.what();
            return false;
        }
    }
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
                if (ext != ".lemesh" && ext != ".leanim" && ext != ".mat" && ext != ".prefab" &&
                    ext != ".level" && ext != ".cubemap" && ext != ".terrain") {
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
        currentFolder.clear();
        selected.clear();
        openedScript.clear();
    }
    // File operations the Qt asset panel drives directly. They used to be
    // private helpers of the immediate-mode draw() function.
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
    bool grid_ = false;
};
