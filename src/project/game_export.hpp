#pragma once
#include "assets/assets.hpp"
#include "project/json_io.hpp"
#include <array>

namespace GameExport {
struct Settings {
    std::string name, company, version, description, copyright, icon, output, player;
    std::string startLevel;
};
void validate(const Settings& settings);
// Builds in a private sibling directory; existing output is never overwritten.
void build(const Settings& settings,
           const AssetManager& assets,
           const std::string& scene,
           const std::filesystem::path& engineData);
struct Mounted {
    std::filesystem::path root;
    Json metadata;
    Mounted() = default;
    Mounted(const Mounted&) = delete;
    Mounted& operator=(const Mounted&) = delete;
    ~Mounted();
};
// Authenticates every chunk before materializing bounded runtime files in a
// private temporary directory. Plaintext is removed on normal shutdown.
void mount(const std::filesystem::path& executable, Mounted& mounted);
} // namespace GameExport
