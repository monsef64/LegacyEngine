#pragma once
#include "assets/assets.hpp"
#include "project/project_io.hpp"
#include <map>
#include <vector>

using ScriptMigration = std::map<std::string, std::string>;
inline void remapLegacyScriptPath(std::string& path, const ScriptMigration& paths) {
    const auto found = paths.find(path);
    if (found != paths.end()) {
        path = found->second;
    }
}
inline std::string remapLegacyScriptLiterals(std::string source, const ScriptMigration& paths) {
    // Only explicit quoted asset paths; never a blind .ls substring replacement.
    for (const auto& pair : paths) {
        for (char quote : {'\'', '"'}) {
            const auto from = quote + pair.first + quote, to = quote + pair.second + quote;
            std::size_t at = 0;
            while ((at = source.find(from, at)) != std::string::npos) {
                source.replace(at, from.size(), to);
                at += to.size();
            }
        }
    }
    return source;
}
inline void remapLegacyScriptJson(Json& value, const ScriptMigration& paths) {
    if (value.is_string()) {
        auto text = value.get<std::string>();
        remapLegacyScriptPath(text, paths);
        value = remapLegacyScriptLiterals(text, paths);
    } else if (value.is_array() || value.is_object()) {
        for (auto& child : value) {
            remapLegacyScriptJson(child, paths);
        }
    }
}
inline void remapLegacyScriptWorld(World& world, const ScriptMigration& paths) {
    world.each<LuaScript>([&](Entity, LuaScript& scripts) {
        auto fields = [&](std::vector<ScriptField>& list) {
            for (auto& f : list) {
                remapLegacyScriptPath(f.text, paths);
            }
        };
        remapLegacyScriptPath(scripts.path, paths);
        fields(scripts.fields);
        for (auto& extra : scripts.additional) {
            remapLegacyScriptPath(extra.path, paths);
            fields(extra.fields);
        }
    });
}
// Validate first, back up every changed source, then write replacements. Originals
// are removed only after every replacement and scene/prefab write has succeeded.
inline ScriptMigration fixProjectAssets(AssetManager& assets, const ProjectIO::Meshes& meshes) {
    const auto files = assets.files();
    ScriptMigration paths;
    for (const auto& file : files) {
        if (AssetManager::extension(file) == ".ls") {
            auto replacement = std::filesystem::path(file);
            replacement.replace_extension(".lua");
            const auto to = replacement.generic_string();
            if (std::filesystem::exists(assets.resolve(to))) {
                throw std::runtime_error("Script migration conflict: " + file + " and " + to +
                                         " both exist. Rename one before Fix Project.");
            }
            paths[file] = to;
        }
    }
    struct ScriptChange {
        std::string from, to, source;
    };
    std::vector<ScriptChange> scripts;
    std::vector<std::pair<std::filesystem::path, Json>> changes;
    for (const auto& file : files) {
        const auto extension = AssetManager::extension(file);
        if (AssetManager::script(file)) {
            const auto original = assets.readScript(file),
                       source = remapLegacyScriptLiterals(original, paths);
            auto to = file;
            remapLegacyScriptPath(to, paths);
            if (to != file || source != original) {
                scripts.push_back({file, to, source});
            }
            continue;
        }
        if (extension != ".level" && extension != ".prefab") {
            continue;
        }
        const auto original = readJsonFile(assets.resolve(file), ProjectIO::maxBytes);
        const bool prefab = extension == ".prefab";
        if (prefab &&
            (original.at("format") != "LegacyEnginePrefab" || original.at("version") != 1)) {
            throw std::runtime_error("Unsupported prefab: " + file);
        }
        ProjectIO::Loaded loaded;
        ProjectIO::decode((prefab ? original.at("scene") : original).dump(), loaded, meshes);
        auto cleaned = parseJson(ProjectIO::encode(
            loaded.world, loaded.source, loaded.camera, meshes, loaded.assetDirectory));
        remapLegacyScriptJson(cleaned, paths);
        if (prefab) {
            cleaned = Json{{"format", "LegacyEnginePrefab"}, {"version", 1}, {"scene", cleaned}};
        }
        changes.emplace_back(assets.resolve(file), std::move(cleaned));
    }
    auto backup = [](const std::filesystem::path& path) {
        if (!std::filesystem::exists(path.string() + ".before-fix.bak")) {
            std::filesystem::copy_file(path, path.string() + ".before-fix.bak");
        }
    };
    for (const auto& s : scripts) {
        backup(assets.resolve(s.from));
    }
    for (const auto& change : changes) {
        backup(change.first);
    }
    for (const auto& s : scripts) {
        assets.writeScript(s.to, s.source);
    }
    for (const auto& change : changes) {
        writeJsonFile(change.first, change.second);
    }
    for (const auto& s : scripts) {
        if (s.from != s.to) {
            std::filesystem::remove(assets.resolve(s.from));
        }
    }
    return paths;
}
