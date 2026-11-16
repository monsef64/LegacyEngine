#pragma once
#include "project/json_io.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <vector>
class RecentProjects {
    std::vector<std::string> paths_;
    std::filesystem::path storage() const {
        const char* path = SDL_GetPrefPath("LegacyEngine", "Editor");
        return path ? std::filesystem::path(path) / "recent-projects.json"
                    : std::filesystem::path{};
    }
    static std::string normalized(const std::string& path) {
        return std::filesystem::weakly_canonical(std::filesystem::absolute(path)).generic_string();
    }
    static std::string key(std::string path) {
#ifdef _WIN32
        std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
#endif
        return path;
    }

  public:
    RecentProjects() {
        try {
            for (const auto& path : readJsonFile(storage(), 65536)) {
                remember(path.get<std::string>(), false);
            }
        } catch (const std::exception&) {
        }
    }
    const auto& paths() const {
        return paths_;
    }
    void remember(const std::string& path, bool save = true) {
        try {
            const auto value = normalized(path), id = key(value);
            paths_.erase(std::remove_if(paths_.begin(),
                                        paths_.end(),
                                        [&](const auto& p) { return key(p) == id; }),
                         paths_.end());
            if (save) {
                paths_.insert(paths_.begin(), value);
            } else {
                paths_.push_back(value);
            }
            if (paths_.size() > 12) {
                paths_.resize(12);
            }
            if (save && !storage().empty()) {
                writeJsonFile(storage(), paths_);
            }
        } catch (const std::exception&) {
        }
    }
};
