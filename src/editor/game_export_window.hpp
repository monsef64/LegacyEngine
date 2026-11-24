#pragma once
#include "levels/level_system.hpp"
#include "platform/project_dialog.hpp"
#include "project/game_export.hpp"
#include "project/project_io.hpp"
#include <SDL3/SDL.h>
#include <future>
#include <imgui.h>

class GameExportWindow {
    char name_[65]{}, company_[257]{}, version_[33] = "1.0.0", description_[1025]{},
                                       copyright_[1025]{};
    char icon_[4097]{}, output_[4097]{}, player_[4097]{};
    std::string scene_, error_, status_;
    std::future<std::string> job_;
    ProjectDialog picker_;
    int picking_ = 0, chunkMiB_ = 16;
    std::filesystem::path root_, pickerRoot_, jobRoot_;
    Json observed_, saved_;
    double editedAt_ = 0;
    bool dirty_ = false;
    static std::filesystem::path settingsFile(const std::filesystem::path& root) {
        return (root.filename() == "Assets" ? root.parent_path() : root) / "Settings" /
               "Export.json";
    }
    Json settings() const {
        return {{"version", 1},
                {"name", name_},
                {"company", company_},
                {"gameVersion", version_},
                {"description", description_},
                {"copyright", copyright_},
                {"icon", icon_},
                {"output", output_},
                {"player", player_},
                {"startScene", scene_},
                {"chunkMiB", chunkMiB_}};
    }
    void saveSettings() {
        if (!dirty_ || root_.empty()) {
            return;
        }
        auto document = settings();
        writeJsonFile(settingsFile(root_), document);
        saved_ = observed_ = std::move(document);
        dirty_ = false;
    }
    void loadSettings(const AssetManager& assets) {
        if (root_ == assets.root) {
            return;
        }
        try {
            saveSettings();
        } catch (const std::exception& e) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Export settings: %s", e.what());
        }
        root_ = assets.root;
        *name_ = *company_ = *description_ = *copyright_ = *icon_ = *output_ = *player_ = 0;
        std::snprintf(version_, sizeof(version_), "1.0.0");
        scene_.clear();
        chunkMiB_ = 16;
        error_.clear();
        status_.clear();
        dirty_ = false;
        try {
            const auto file = settingsFile(root_);
            if (!root_.empty() && std::filesystem::exists(file)) {
                const auto j = readJsonFile(file, 32768);
                if (j.at("version") != 1) {
                    throw std::runtime_error("Unsupported export settings version");
                }
                // Validate every value before applying anything; malformed files
                // cannot leak partial or truncated metadata into an export.
                auto read = [&](const char* key, std::size_t capacity, const char* fallback = "") {
                    auto value = j.value(key, std::string(fallback));
                    if (value.size() >= capacity || value.find('\0') != value.npos) {
                        throw std::runtime_error(std::string("Invalid export setting: ") + key);
                    }
                    return value;
                };
                const auto name = read("name", sizeof(name_)),
                           company = read("company", sizeof(company_)),
                           version = read("gameVersion", sizeof(version_), "1.0.0"),
                           description = read("description", sizeof(description_)),
                           copyright = read("copyright", sizeof(copyright_)),
                           icon = read("icon", sizeof(icon_)),
                           output = read("output", sizeof(output_)),
                           player = read("player", sizeof(player_)),
                           scene = read("startScene", 129);
                const int chunk = j.value("chunkMiB", 16);
                if (chunk < 1 || chunk > 64) {
                    throw std::runtime_error("Invalid export chunk size");
                }
                if (!scene.empty() && !LevelSystem::validName(scene)) {
                    throw std::runtime_error("Invalid export start scene");
                }
                std::snprintf(name_, sizeof(name_), "%s", name.c_str());
                std::snprintf(company_, sizeof(company_), "%s", company.c_str());
                std::snprintf(version_, sizeof(version_), "%s", version.c_str());
                std::snprintf(description_, sizeof(description_), "%s", description.c_str());
                std::snprintf(copyright_, sizeof(copyright_), "%s", copyright.c_str());
                std::snprintf(icon_, sizeof(icon_), "%s", icon.c_str());
                std::snprintf(output_, sizeof(output_), "%s", output.c_str());
                std::snprintf(player_, sizeof(player_), "%s", player.c_str());
                scene_ = scene;
                chunkMiB_ = chunk;
            }
        } catch (const std::exception& e) {
            error_ = std::string("Cannot load export settings: ") + e.what();
        }
        saved_ = observed_ = settings();
    }
    void persistEdits(bool force = false) {
        auto current = settings();
        if (current != observed_) {
            observed_ = std::move(current);
            editedAt_ = ImGui::GetTime();
            dirty_ = observed_ != saved_;
        }
        if (dirty_ && (force || ImGui::GetTime() - editedAt_ >= .4 || !ImGui::IsAnyItemActive())) {
            try {
                saveSettings();
            } catch (const std::exception& e) {
                error_ = std::string("Cannot save export settings: ") + e.what();
            }
        }
    }

  public:
    ~GameExportWindow() {
        try {
            saveSettings();
        } catch (const std::exception& e) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Export settings: %s", e.what());
        }
    }
    void saveProjectSettings(const AssetManager& source, const AssetManager& destination) {
        loadSettings(source);
        writeJsonFile(settingsFile(destination.root), settings());
    }
    bool visible = false;
    bool busy() const {
        return job_.valid();
    }
    void draw(World& world,
              const AssetManager& assets,
              const std::string& source,
              const OrbitCamera& camera,
              const ProjectIO::Meshes& meshes,
              const LevelSystem& levels,
              bool allowed) {
        loadSettings(assets);
        if (auto result = picker_.poll(); result && pickerRoot_ == root_) {
            if (!result->error.empty()) {
                error_ = result->error;
            }
            if (!result->path.empty()) {
                auto path = result->path;
                if (picking_ == 2) {
                    path = (std::filesystem::path(path) /
                            (std::string(name_).empty() ? "Game" : name_))
                               .string();
                }
                char* target = picking_ == 1 ? icon_ : picking_ == 2 ? output_ : player_;
                std::snprintf(target, 4097, "%s", path.c_str());
            }
        }
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = job_.get();
                if (jobRoot_ == root_) {
                    status_ = std::move(result);
                    error_.clear();
                }
            } catch (const std::exception& error) {
                if (jobRoot_ == root_) {
                    error_ = error.what();
                    status_.clear();
                }
            }
        }
        if (!visible && !busy()) {
            persistEdits(true); // Includes a dialog result delivered after closing the window.
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(650, 610), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Export Game", &visible)) {
            ImGui::BeginDisabled(busy() || !allowed || picker_.pending());
            ImGui::InputText("Game Name", name_, sizeof(name_));
            ImGui::InputText("Company (optional)", company_, sizeof(company_));
            ImGui::InputText("Version", version_, sizeof(version_));
            ImGui::InputText("Description (optional)", description_, sizeof(description_));
            ImGui::InputText("Copyright (optional)", copyright_, sizeof(copyright_));
            if (ImGui::BeginCombo("Start scene",
                                  scene_.empty() ? "Current Scene" : scene_.c_str())) {
                if (ImGui::Selectable("Current Scene", scene_.empty())) {
                    scene_.clear();
                }
                for (const auto& name : levels.names()) {
                    if (ImGui::Selectable(name.c_str(), scene_ == name)) {
                        scene_ = name;
                    }
                }
                ImGui::EndCombo();
            }
            auto browse = [&](int kind, const char* label) {
                ImGui::SameLine();
                if (ImGui::Button(label)) {
                    picking_ = kind;
                    pickerRoot_ = root_;
#ifdef _WIN32
                    static const SDL_DialogFileFilter iconFilter[]{{"Icon", "ico"}};
                    static const SDL_DialogFileFilter playerFilter[]{{"Player", "exe"}};
#else
                    static const SDL_DialogFileFilter iconFilter[]{{"Icon", "png"}};
                    static const SDL_DialogFileFilter playerFilter[]{{"Player", "*"}};
#endif
                    picker_.chooseExportPath(SDL_GL_GetCurrentWindow(),
                                             kind == 2,
                                             kind == 1 ? iconFilter : playerFilter,
                                             1);
                }
            };
            ImGui::InputText("Icon file (absolute path)", icon_, sizeof(icon_));
            browse(1, "Browse##Icon");
            ImGui::InputText("New output folder", output_, sizeof(output_));
            browse(2, "Browse##Output");
            if (!*player_) {
#ifdef _WIN32
                const auto file = std::filesystem::path(SDL_GetBasePath()) / "LegacyPlayer.exe";
#else
                const auto file = std::filesystem::path(SDL_GetBasePath()) / "LegacyPlayer";
#endif
                std::snprintf(player_, sizeof(player_), "%s", file.string().c_str());
            }
            ImGui::InputText("Native player template", player_, sizeof(player_));
            browse(3, "Browse##Player");
            ImGui::SliderInt("Chunk size (MiB)", &chunkMiB_, 1, 64);
            if (ImGui::Button("Export Game")) {
                try {
                    persistEdits(true);
                    GameExport::Settings settings{name_,
                                                  company_,
                                                  version_,
                                                  description_,
                                                  copyright_,
                                                  icon_,
                                                  output_,
                                                  player_};
                    settings.chunkMiB = unsigned(chunkMiB_);
                    settings.startLevel = scene_.empty() ? levels.current() : scene_;
                    GameExport::validate(settings);
                    std::string scene;
                    if (scene_.empty()) {
                        scene = ProjectIO::encode(world, source, camera, meshes, "Assets");
                    } else {
                        World loaded;
                        std::string script, error;
                        OrbitCamera pose;
                        if (!ProjectIO::load(assets.resolve(LevelSystem::path(scene_)).string(),
                                             loaded,
                                             script,
                                             pose,
                                             meshes,
                                             error)) {
                            throw std::runtime_error(error);
                        }
                        scene = ProjectIO::encode(loaded, script, pose, meshes, "Assets");
                    }
                    const auto engineData = std::filesystem::path(SDL_GetBasePath()) / "data";
                    jobRoot_ = root_;
                    job_ = std::async(std::launch::async,
                                      [settings, assets, scene = std::move(scene), engineData] {
                                          GameExport::build(settings, assets, scene, engineData);
                                          return "Exported to " + settings.output;
                                      });
                    status_ = "Exporting...";
                    error_.clear();
                } catch (const std::exception& error) {
                    error_ = error.what();
                }
            }
            ImGui::EndDisabled();
            if (!status_.empty()) {
                ImGui::TextWrapped("%s", status_.c_str());
            }
            if (!error_.empty()) {
                ImGui::TextWrapped("%s", error_.c_str());
            }
        }
        ImGui::End();
        persistEdits(!visible);
    }
};
