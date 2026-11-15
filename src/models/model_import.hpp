#pragma once
#include "models/model_asset.hpp"
// Model import is restricted to OBJ and glTF/GLB, including animated glTF.
std::string modelImportExtensions();
bool importableModel(const std::filesystem::path& path);
std::string importModel(AssetManager& assets,
                        const std::filesystem::path& source,
                        const std::string& folder = {},
                        bool publishRevision = true);
