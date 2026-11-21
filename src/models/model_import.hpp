#pragma once
#include "models/model_asset.hpp"
// Engine source-format allowlist; internal Assimp dependencies are not advertised.
std::string modelImportExtensions();
bool importableModel(const std::filesystem::path& path);
std::string importModel(AssetManager& assets,
                        const std::filesystem::path& source,
                        const std::string& folder = {},
                        bool publishRevision = true);
