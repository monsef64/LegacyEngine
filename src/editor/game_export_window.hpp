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

  public:
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
        if (auto result = picker_.poll()) {
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
                status_ = job_.get();
                error_.clear();
            } catch (const std::exception& error) {
                error_ = error.what();
                status_.clear();
            }
        }
        if (!visible && !busy()) {
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
    }
};
