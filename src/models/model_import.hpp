#pragma once
#include "models/model_asset.hpp"
// Supported extensions come from the importers enabled in the bundled Assimp.
std::string modelImportExtensions();
bool importableModel(const std::filesystem::path& path);
std::string importModel(AssetManager& assets,
                        const std::filesystem::path& source,
                        const std::string& folder = {});
