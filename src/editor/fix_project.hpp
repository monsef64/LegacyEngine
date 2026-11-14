#pragma once
#include "assets/assets.hpp"
#include "project/project_io.hpp"
#include <vector>
// Round-trip through the current schema to discard retired fields/components.
// Validate everything first and preserve byte-for-byte backups before replacing files.
inline void fixProjectAssets(const AssetManager& assets, const ProjectIO::Meshes& meshes) {
    std::vector<std::pair<std::filesystem::path, Json>> changes;
    for (const auto& file : assets.files()) {
        const auto extension = AssetManager::extension(file);
        if (extension != ".level" && extension != ".prefab") {
            continue;
        }
        const auto path = assets.resolve(file);
        const auto original = readJsonFile(path, ProjectIO::maxBytes);
        const bool prefab = extension == ".prefab";
        if (prefab &&
            (original.at("format") != "LegacyEnginePrefab" || original.at("version") != 1)) {
            throw std::runtime_error("Unsupported prefab: " + file);
        }
        ProjectIO::Loaded loaded;
        ProjectIO::decode((prefab ? original.at("scene") : original).dump(), loaded, meshes);
        auto cleaned = parseJson(ProjectIO::encode(
            loaded.world, loaded.source, loaded.camera, meshes, loaded.assetDirectory));
        if (prefab) {
            cleaned = Json{{"format", "LegacyEnginePrefab"}, {"version", 1}, {"scene", cleaned}};
        }
        changes.emplace_back(path, std::move(cleaned));
    }
    for (const auto& item : changes) {
        if (!std::filesystem::exists(item.first.string() + ".before-fix.bak")) {
            std::filesystem::copy_file(item.first, item.first.string() + ".before-fix.bak");
        }
    }
    for (const auto& item : changes) {
        writeJsonFile(item.first, item.second);
    }
}
