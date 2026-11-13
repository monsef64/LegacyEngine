#pragma once
#include "core/components.hpp"
#include "core/ecs.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

class AssetManager {
  public:
    std::filesystem::path root; // Must be supplied by the owning project.
    std::string error;
    static std::string extension(const std::filesystem::path& p) {
        auto e = p.extension().string();
        for (char& c : e) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return e;
    }
    static bool script(const std::filesystem::path& p) {
        return extension(p) == ".ls" || extension(p) == ".lua";
    }
    std::filesystem::path resolve(const std::string& relative) const {
        if (root.empty()) {
            throw std::runtime_error("No project asset root is configured");
        }
        const std::filesystem::path p(relative);
        if (relative.find('\0') != std::string::npos) {
            throw std::runtime_error("Invalid asset path");
        }
        if (p.is_absolute()) {
            throw std::runtime_error("Asset path must be relative");
        }
        for (const auto& part : p) {
            if (part == "..") {
                throw std::runtime_error("Asset path escapes Assets");
            }
        }
        auto candidate = std::filesystem::weakly_canonical(root / p);
        auto canonicalRoot = std::filesystem::weakly_canonical(root);
        auto r = candidate.lexically_relative(canonicalRoot);
        if (r.empty() || *r.begin() == "..") {
            throw std::runtime_error("Asset path escapes Assets");
        }
        return candidate;
    }
    std::string createFolder(const std::string& parent = {}) const {
        const auto base = resolve(parent);
        std::filesystem::create_directories(base);
        std::string name = "New Folder";
        unsigned i = 2;
        while (std::filesystem::exists(base / name)) {
            name = "New Folder (" + std::to_string(i++) + ")";
        }
        const auto relative = (std::filesystem::path(parent) / name).generic_string();
        std::filesystem::create_directory(resolve(relative));
        return relative;
    }
    std::vector<std::string> files() const {
        std::vector<std::string> result;
        if (root.empty() || !std::filesystem::exists(root)) {
            return result;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (result.size() >= 10000) {
                break;
            }
            if (entry.is_regular_file() && !entry.is_symlink()) {
                auto relative = entry.path().lexically_relative(root).generic_string();
                resolve(relative);
                result.push_back(relative);
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }
    std::string import(const std::filesystem::path& source) {
        if (root.empty()) {
            throw std::runtime_error("No project asset root is configured");
        }
        if (!std::filesystem::is_regular_file(source)) {
            throw std::runtime_error("Drop individual files, not directories");
        }
        if (std::filesystem::file_size(source) > 256 * 1024 * 1024) {
            throw std::runtime_error("Asset exceeds 256 MiB import limit");
        }
        std::filesystem::create_directories(root);
        if (source.parent_path() == root) {
            return source.filename().generic_string();
        }
        auto name = source.filename();
        int n = 2;
        while (std::filesystem::exists(root / name)) {
            if (std::filesystem::equivalent(source, root / name)) {
                return name.generic_string();
            }
            name = source.stem().string() + " (" + std::to_string(n++) + ")" +
                   source.extension().string();
        }
        std::filesystem::copy_file(source, root / name);
        return name.generic_string();
    }
    std::string readScript(const std::string& path) const {
        auto file = resolve(path);
        if (!script(file) || std::filesystem::file_size(file) > 64 * 1024) {
            throw std::runtime_error("Expected a LegacyScript asset, at most 64 KiB");
        }
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Cannot read LegacyScript asset");
        }
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    void writeScript(const std::string& path, std::string_view source) {
        if (!script(path) || source.size() > 64 * 1024) {
            throw std::runtime_error("Invalid LegacyScript asset");
        }
        auto destination = resolve(path);
        std::filesystem::create_directories(destination.parent_path());
        auto temp = destination;
        temp += ".writing-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(source.data(), source.size());
        out.close();
        if (!out) {
            throw std::runtime_error("Cannot save LegacyScript asset");
        }
        std::filesystem::rename(temp, destination);
    }
    // Save As copies the asset tree without changing existing project assets.
    // Conflicting content is rejected rather than silently breaking references.
    void copyTo(const std::filesystem::path& destination) const {
        if (root.empty()) {
            throw std::runtime_error("No project asset root is configured");
        }
        const auto sourceRoot = std::filesystem::weakly_canonical(root),
                   targetRoot = std::filesystem::weakly_canonical(destination);
        const auto nested = targetRoot.lexically_relative(sourceRoot);
        if (!nested.empty() && nested != "." && *nested.begin() != "..") {
            throw std::runtime_error("Cannot copy Assets into its own subfolder");
        }
        std::filesystem::create_directories(destination);
        if (std::filesystem::weakly_canonical(root) ==
            std::filesystem::weakly_canonical(destination)) {
            return;
        }
        if (std::filesystem::exists(root)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
                if (entry.is_directory() && !entry.is_symlink()) {
                    auto relative = entry.path().lexically_relative(root).generic_string();
                    resolve(relative);
                    std::filesystem::create_directories(destination / relative);
                }
            }
        }
        for (const auto& path : files()) {
            const auto from = resolve(path), to = destination / path;
            std::filesystem::create_directories(to.parent_path());
            if (std::filesystem::exists(to)) {
                std::ifstream a(from, std::ios::binary), b(to, std::ios::binary);
                if (std::string(std::istreambuf_iterator<char>(a), {}) !=
                    std::string(std::istreambuf_iterator<char>(b), {})) {
                    throw std::runtime_error("Save As asset conflict: " + path);
                }
            } else {
                std::filesystem::copy_file(from, to);
            }
        }
    }
};
inline bool attachScriptAsset(World& world, Entity entity, const std::string& path) {
    if (!world.alive(entity) || !AssetManager::script(path)) {
        return false;
    }
    if (std::filesystem::path(path).is_absolute()) {
        return false;
    }
    for (const auto& part : std::filesystem::path(path)) {
        if (part == "..") {
            return false;
        }
    }
    if (auto* scripts = world.tryGet<LuaScript>(entity)) {
        for (const auto& entry : scriptAttachments(*scripts)) {
            if (entry.path == path) {
                return true;
            }
        }
        if (scripts->path.empty()) {
            scripts->path = path;
            scripts->enabled = true;
        } else if (scripts->additional.size() < 63) {
            scripts->additional.push_back({path, true});
        } else {
            return false;
        }
    } else {
        world.add(entity, LuaScript{path, true, {}});
    }
    return true;
}
