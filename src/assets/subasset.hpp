#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
// Reserved reference notation: relative/model.mesh#Mesh_0.mesh (or .leanim).
// Only the container is a filesystem object; members are JSON subdocuments.
inline bool embeddedAsset(const std::string& path) {
    const auto split = path.find('#');
    if (split == std::string::npos) {
        return false;
    }
    const auto extension = std::filesystem::path(path.substr(0, split)).extension();
    return extension == ".mesh" || extension == ".lemesh";
}
inline std::filesystem::path assetContainerPath(const std::filesystem::path& path) {
    const auto value = path.generic_string();
    return embeddedAsset(value) ? std::filesystem::path(value.substr(0, value.find('#'))) : path;
}
inline std::string embeddedAssetName(const std::string& path) {
    const auto name = path.substr(path.find('#') + 1);
    if (name.empty() || name.size() > 256 || name == "." || name == ".." ||
        name.find_first_of("/\\#") != std::string::npos || name.find('\0') != std::string::npos) {
        throw std::runtime_error("Invalid embedded asset member");
    }
    return name;
}
