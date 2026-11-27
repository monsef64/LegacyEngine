#pragma once
#include "models/model_import.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Main-thread ImGui picker. Keep the request/poll API used by editor consumers.
class ProjectDialog {
  public:
    struct Filter {
        const char* name;
        const char* pattern;
    };
    struct Result {
        bool saving = false;
        std::string path, error;
        std::vector<std::string> paths;
    };
    ProjectDialog() {
        instances().push_back(this);
    }
    ~ProjectDialog() {
        auto& list = instances();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }
    ProjectDialog(const ProjectDialog&) = delete;
    ProjectDialog& operator=(const ProjectDialog&) = delete;
    bool pending() const {
        return busy_;
    }
    void openRecent(const std::string& path) {
        if (!busy_) {
            result_ = Result{false, path, {}};
        }
    }
    void show(SDL_Window*, bool saving, const std::string& path) {
        static const Filter filters[]{{"LegacyEngine project", "legacy"}};
        begin(saving, false, false, filters, 1, path);
    }
    void showImport(SDL_Window*) {
        const std::string extensions = modelImportExtensions();
        const Filter filters[]{{"3D Models", extensions.c_str()},
                               {"glTF / GLB", "gltf;glb"},
                               {"Wavefront OBJ", "obj"},
                               {"Half-Life MDL", "mdl"},
                               {"LegacyEngine Mesh", "lemesh"},
                               {"Scripts", "ls;lua"},
                               {"Images", "png;jpg;jpeg;bmp;tga;hdr"},
                               {"Audio", "wav;mp3;ogg;flac"},
                               {"All files", "*"}};
        begin(false, false, true, filters, int(std::size(filters)), {});
    }
    void showImportDirectory(SDL_Window*) {
        begin(false, true, true, nullptr, 0, {});
    }
    void chooseExportPath(SDL_Window*, bool folder, const Filter* filters, int count) {
        begin(false, folder, false, filters, count, {});
    }
    std::optional<Result> poll() {
        if (!result_) {
            return {};
        }
        auto result = std::move(result_);
        result_.reset();
        busy_ = false;
        return result;
    }
    // Called once per editor frame, outside all parent windows/disabled scopes.
    // A picker remains drawable even if its originating tool window is closed.
    static void drawAll() {
        for (auto* picker : instances()) {
            if (picker->busy_ && !picker->result_) {
                picker->draw();
                break;
            }
        }
    }

  private:
    struct Entry {
        std::filesystem::path path;
        bool directory;
    };
    struct OwnedFilter {
        std::string name, pattern;
    };
    bool busy_ = false, saving_ = false, folder_ = false, multiple_ = false;
    bool opening_ = false, overwrite_ = false;
    int filter_ = 0;
    std::filesystem::path directory_, overwritePath_;
    std::array<char, 4096> path_{}, filename_{};
    std::vector<OwnedFilter> filters_;
    std::vector<Entry> entries_;
    std::set<std::filesystem::path> selected_;
    std::optional<Result> result_;
    std::string error_;
    static std::vector<ProjectDialog*>& instances() {
        static std::vector<ProjectDialog*> list;
        return list;
    }
    static std::string lower(std::string value) {
        for (auto& c : value) {
            c = char(std::tolower(static_cast<unsigned char>(c)));
        }
        return value;
    }
    template <std::size_t N>
    static void text(std::array<char, N>& target, const std::string& value) {
        std::snprintf(target.data(), target.size(), "%s", value.c_str());
    }
    void begin(bool saving,
               bool folder,
               bool multiple,
               const Filter* filters,
               int count,
               const std::string& initial) {
        if (busy_) {
            return;
        }
        busy_ = opening_ = true;
        saving_ = saving;
        folder_ = folder;
        multiple_ = multiple;
        overwrite_ = false;
        filter_ = 0;
        result_.reset();
        filters_.clear();
        selected_.clear();
        error_.clear();
        filename_.fill(0);
        for (int i = 0; i < count; ++i) {
            filters_.push_back({filters[i].name, filters[i].pattern});
        }
        std::error_code ec;
        if (directory_.empty()) {
            directory_ = std::filesystem::current_path(ec);
        }
        if (!initial.empty()) {
            auto p = std::filesystem::absolute(initial, ec);
            if (!ec) {
                if (std::filesystem::is_directory(p, ec)) {
                    directory_ = p;
                } else {
                    directory_ = p.parent_path();
                    text(filename_, p.filename().string());
                }
            }
        } else if (saving_) {
            text(filename_, "project.legacy");
        }
        refresh();
    }
    bool matches(const std::filesystem::path& path) const {
        if (filters_.empty()) {
            return true;
        }
        const auto& pattern = filters_[filter_].pattern;
        const auto ext = lower(path.extension().string());
        std::size_t start = 0;
        while (start <= pattern.size()) {
            auto end = pattern.find(';', start);
            auto token = lower(pattern.substr(start, end - start));
            if (token == "*" || (!token.empty() && ext == "." + token)) {
                return true;
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return false;
    }
    void refresh() {
        entries_.clear();
        selected_.clear();
        error_.clear();
        text(path_, directory_.string());
        std::error_code ec;
        std::filesystem::directory_iterator it(directory_, ec), end;
        while (!ec && it != end) {
            const bool dir = it->is_directory(ec);
            if (!ec && (dir || (!folder_ && matches(it->path())))) {
                entries_.push_back({it->path(), dir});
            }
            if (!ec) {
                it.increment(ec);
            }
        }
        if (ec) {
            error_ = ec.message();
        }
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            return a.directory != b.directory ? a.directory > b.directory
                                              : a.path.filename() < b.path.filename();
        });
    }
    void navigate(std::filesystem::path p) {
        if (p.is_relative()) {
            p = directory_ / p;
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(p, ec)) {
            error_ = ec ? ec.message() : "Directory does not exist";
            return;
        }
        directory_ = p.lexically_normal();
        filename_.fill(0);
        refresh();
    }
    void finish(const std::vector<std::filesystem::path>& paths) {
        Result result;
        result.saving = saving_;
        for (const auto& p : paths) {
            result.paths.push_back(p.string());
        }
        if (!result.paths.empty()) {
            result.path = result.paths.front();
        }
        result_ = std::move(result);
        ImGui::CloseCurrentPopup();
    }
    void accept() {
        std::vector<std::filesystem::path> paths;
        if (saving_ || filename_[0]) {
            std::filesystem::path p(filename_.data());
            if (p.is_relative()) {
                p = directory_ / p;
            }
            if (saving_ && p.extension().empty()) {
                p += ".legacy";
            }
            paths.push_back(p.lexically_normal());
        } else if (!selected_.empty()) {
            paths.assign(selected_.begin(), selected_.end());
        } else if (folder_) {
            paths.push_back(directory_);
        }
        if (paths.empty()) {
            error_ = "Select a file or enter a filename";
            return;
        }
        for (const auto& p : paths) {
            std::error_code ec;
            const bool dir = std::filesystem::is_directory(p, ec);
            if (saving_) {
                ec.clear();
                if (dir || !std::filesystem::is_directory(p.parent_path(), ec)) {
                    error_ = "Choose a filename in an existing directory";
                    return;
                }
            } else if (folder_ ? !dir : !std::filesystem::is_regular_file(p, ec)) {
                error_ = folder_ ? "Select an existing directory" : "Select an existing file";
                return;
            }
            if (!folder_ && !matches(p)) {
                error_ = "File does not match the selected filter";
                return;
            }
            if (saving_) {
                ec.clear();
                if (std::filesystem::exists(p, ec)) {
                    overwritePath_ = p;
                    overwrite_ = true;
                    return;
                }
                if (ec) {
                    error_ = ec.message();
                    return;
                }
            }
        }
        finish(paths);
    }
    void draw() {
        const std::string id =
            "File picker###ProjectDialog" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
        if (opening_) {
            ImGui::OpenPopup(id.c_str());
            opening_ = false;
        }
        ImGui::SetNextWindowSize(ImVec2(720, 510), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(420, 380), ImVec2(10000, 10000));
        bool open = true;
        if (ImGui::BeginPopupModal(id.c_str(), &open)) {
            ImGui::TextUnformatted(saving_   ? "Save project"
                                   : folder_ ? "Select folder"
                                             : "Open file");
            if (ImGui::Button("Up")) {
                navigate(directory_.parent_path());
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) {
                refresh();
            }
            ImGui::SameLine();
            if (ImGui::InputText("Directory",
                                 path_.data(),
                                 path_.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                navigate(path_.data());
            }
#ifdef _WIN32
            for (char drive = 'A'; drive <= 'Z'; ++drive) {
                const std::string root = std::string(1, drive) + ":/";
                std::error_code ec;
                if (std::filesystem::is_directory(root, ec)) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(root.c_str())) {
                        navigate(root);
                    }
                }
            }
#endif
            if (multiple_) {
                ImGui::TextDisabled(
                    "Ctrl+click to select multiple items. Double-click folders to enter.");
            }
            std::optional<std::filesystem::path> enter;
            bool activate = false;
            ImGui::BeginChild("Entries", ImVec2(0, -175), true);
            for (std::size_t i = 0; i < entries_.size(); ++i) {
                const auto& e = entries_[i];
                ImGui::PushID(int(i));
                const std::string label =
                    (e.directory ? "[Folder] " : "") + e.path.filename().string();
                if (ImGui::Selectable(label.c_str(),
                                      selected_.count(e.path) != 0,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    const bool toggle = multiple_ && ImGui::GetIO().KeyCtrl;
                    if (!toggle) {
                        selected_.clear();
                    }
                    if (toggle && selected_.count(e.path)) {
                        selected_.erase(e.path);
                    } else {
                        selected_.insert(e.path);
                    }
                    filename_.fill(0);
                    if (saving_ && !e.directory) {
                        text(filename_, e.path.filename().string());
                    }
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (e.directory) {
                            enter = e.path;
                        } else {
                            activate = true;
                        }
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (enter) {
                navigate(*enter);
            }
            if (!filters_.empty() &&
                ImGui::BeginCombo("File type", filters_[filter_].name.c_str())) {
                for (int i = 0; i < int(filters_.size()); ++i) {
                    if (ImGui::Selectable(filters_[i].name.c_str(), filter_ == i)) {
                        filter_ = i;
                        refresh();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::InputText(folder_ ? "Folder name / path" : "Filename / path",
                             filename_.data(),
                             filename_.size());
            if (ImGui::IsItemEdited()) {
                selected_.clear();
            }
            if (ImGui::Button("Create folder using name")) {
                const std::filesystem::path name(filename_.data());
                std::error_code ec;
                if (name.empty() || name.has_parent_path() || name == "." || name == "..") {
                    error_ = "Enter a new folder name (not a path)";
                } else if (std::filesystem::create_directory(directory_ / name, ec)) {
                    navigate(directory_ / name);
                } else {
                    error_ = ec ? ec.message() : "Folder already exists";
                }
            }
            ImGui::TextWrapped("%s", error_.c_str());
            if (ImGui::Button(saving_ ? "Save" : folder_ ? "Select folder" : "Open") || activate) {
                accept();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                finish({});
            }
            if (overwrite_) {
                ImGui::OpenPopup("Confirm overwrite");
                overwrite_ = false;
            }
            if (ImGui::BeginPopupModal(
                    "Confirm overwrite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Replace existing file?\n%s", overwritePath_.string().c_str());
                if (ImGui::Button("Replace")) {
                    ImGui::CloseCurrentPopup();
                    // Close the parent after this nested modal scope ends.
                    Result r;
                    r.saving = true;
                    r.path = overwritePath_.string();
                    r.paths = {r.path};
                    result_ = std::move(r);
                }
                ImGui::SameLine();
                if (ImGui::Button("Keep existing")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (result_) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        // BeginPopupModal can return false after its title-bar close button.
        if (!open && !result_) {
            Result cancelled;
            cancelled.saving = saving_;
            result_ = std::move(cancelled);
        }
    }
};
