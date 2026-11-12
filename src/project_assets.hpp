#pragma once
#include <filesystem>
#include <chrono>
#include <stdexcept>

// Each saved project gets a private companion asset directory. Untitled assets
// are staged in the OS temp area, never beside the executable/engine sources.
inline std::filesystem::path projectAssetsPath(const std::filesystem::path& project) {
    auto absolute=std::filesystem::absolute(project).lexically_normal();
    return absolute.parent_path()/(absolute.filename().string()+".assets");
}
inline std::filesystem::path existingProjectAssetsPath(const std::filesystem::path& project) {
    auto path=projectAssetsPath(project);
    if(std::filesystem::exists(path))return path;
    auto legacy=std::filesystem::absolute(project).parent_path()/"Assets";
    return std::filesystem::is_directory(legacy)?legacy:path;
}
class UntitledProjectAssets {
public:
    std::filesystem::path path;
    UntitledProjectAssets() {
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned i=0;i<100;++i) {
            auto candidate=std::filesystem::temp_directory_path()/("LegacyEngine-untitled-"+std::to_string(stamp)+"-"+std::to_string(i));
            if(std::filesystem::create_directory(candidate)){path=candidate;return;}
        }
        throw std::runtime_error("Cannot create temporary project asset storage");
    }
    UntitledProjectAssets(const UntitledProjectAssets&)=delete;
    UntitledProjectAssets& operator=(const UntitledProjectAssets&)=delete;
    ~UntitledProjectAssets(){std::error_code error;if(!path.empty())std::filesystem::remove_all(path,error);}
};
