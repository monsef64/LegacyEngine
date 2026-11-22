#pragma once
#include "levels/level_system.hpp"
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
            ImGui::TextUnformatted("Standalone game export");
#ifdef _WIN32
            ImGui::TextDisabled("Native target: Windows x64 (MinGW-w64); icon: .ico");
#else
            ImGui::TextDisabled("Native target: Linux x86_64; icon: .png");
#endif
            ImGui::TextWrapped(
                "Export Windows builds on Windows and Linux builds on Linux. Build/install LegacyPlayer beside the editor first. All fields below are required.");
            ImGui::BeginDisabled(busy() || !allowed);
            ImGui::InputText("Game Name", name_, sizeof(name_));
            ImGui::InputText("Company", company_, sizeof(company_));
            ImGui::InputText("Version", version_, sizeof(version_));
            ImGui::InputText("Description", description_, sizeof(description_));
            ImGui::InputText("Copyright", copyright_, sizeof(copyright_));
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
            ImGui::InputText("Icon file (absolute path)", icon_, sizeof(icon_));
            ImGui::InputText("New output folder", output_, sizeof(output_));
            if (!*player_) {
#ifdef _WIN32
                const auto file = std::filesystem::path(SDL_GetBasePath()) / "LegacyPlayer.exe";
#else
                const auto file = std::filesystem::path(SDL_GetBasePath()) / "LegacyPlayer";
#endif
                std::snprintf(player_, sizeof(player_), "%s", file.string().c_str());
            }
            ImGui::InputText("Native player template", player_, sizeof(player_));
            ImGui::TextWrapped(
                "Output: GameName.exe (Windows) / GameName (Linux), plus GameName_Data with authenticated encrypted 16 MiB chunks. Required Windows DLLs are copied beside the EXE.");
            ImGui::TextWrapped(
                "Encryption deters casual browsing, not extraction: the player contains its decryption key and materializes private temporary runtime files. These are removed on normal shutdown; crashes may leave them behind.");
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
            if (busy()) {
                ImGui::TextDisabled(
                    "Keep the editor open until export finishes. Existing output is never overwritten.");
            }
        }
        ImGui::End();
    }
};
