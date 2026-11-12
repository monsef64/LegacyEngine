#pragma once
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

// Linux freedesktop Trash: files + .trashinfo are visible to the desktop trash
// manager. Keep a receipt so scene history can restore the exact trashed file.
class TrashFile {
public:
    std::filesystem::path original,trashed,info;
    explicit TrashFile(std::filesystem::path path):original(std::filesystem::absolute(path)) {}
    void remove() {
#if defined(__linux__)
        const char* xdg=std::getenv("XDG_DATA_HOME");const char* home=std::getenv("HOME");
        if(!home && !(xdg && std::filesystem::path(xdg).is_absolute()))throw std::runtime_error("Cannot locate system Trash");
        std::filesystem::path base=(xdg && std::filesystem::path(xdg).is_absolute())?
            std::filesystem::path(xdg):std::filesystem::path(home?home:"")/".local/share";
        auto root=base/"Trash";auto files=root/"files",metadata=root/"info";
        std::filesystem::create_directories(files);std::filesystem::create_directories(metadata);
        for(const auto& directory:{root,files,metadata})
            std::filesystem::permissions(directory,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        std::string name=original.filename().string()+"-"+std::to_string(stamp);
        for(unsigned n=0;std::filesystem::exists(files/name) || std::filesystem::exists(metadata/(name+".trashinfo"));++n)
            name=original.filename().string()+"-"+std::to_string(stamp)+"-"+std::to_string(n);
        trashed=files/name;info=metadata/(name+".trashinfo");
        if(!std::filesystem::is_regular_file(original) || std::filesystem::is_symlink(original))throw std::runtime_error("Asset no longer exists or is not a regular file");
        std::ofstream out(info,std::ios::binary);
        const auto now=std::time(nullptr);const auto local=*std::localtime(&now);
        out<<"[Trash Info]\nPath="<<encoded(original.string())<<"\nDeletionDate="<<std::put_time(&local,"%Y-%m-%dT%H:%M:%S")<<'\n';out.close();
        if(!out){std::error_code ec;std::filesystem::remove(info,ec);throw std::runtime_error("Cannot write system Trash metadata");}
        try{move(original,trashed);}catch(...){std::error_code ec;std::filesystem::remove(info,ec);throw;}
#elif defined(__APPLE__)
        const char* home=std::getenv("HOME");if(!home)throw std::runtime_error("Cannot locate system Trash");
        auto root=std::filesystem::path(home)/".Trash";std::filesystem::create_directories(root);
        trashed=root/(original.filename().string()+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        move(original,trashed);
#else
        throw std::runtime_error("System Trash is not supported on this platform; asset was not deleted");
#endif
    }
    void restore() {
        if(std::filesystem::exists(original))throw std::runtime_error("Cannot undo deletion: an asset already exists at "+original.string());
        if(trashed.empty() || !std::filesystem::exists(trashed))throw std::runtime_error("Cannot undo deletion: the asset is no longer in Trash");
        std::filesystem::create_directories(original.parent_path());move(trashed,original);
        if(!info.empty()){std::error_code ec;std::filesystem::remove(info,ec);}
    }
private:
    static std::string encoded(const std::string& path) {
        const char* hex="0123456789ABCDEF";std::string result;
        for(unsigned char c:path) {
            if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='/' || c=='-' || c=='_' || c=='.')result+=static_cast<char>(c);
            else {result+='%';result+=hex[c>>4];result+=hex[c&15];}
        }
        return result;
    }
    static void move(const std::filesystem::path& from,const std::filesystem::path& to) {
        if(std::filesystem::exists(to))throw std::runtime_error("Trash/restore destination already exists");
        std::error_code ec;std::filesystem::rename(from,to,ec);
        if(!ec)return;
        if(ec!=std::errc::cross_device_link)throw std::filesystem::filesystem_error("Cannot move asset",from,to,ec);
        std::filesystem::copy_file(from,to); // No overwrite; keep original if copy fails.
        try{std::filesystem::remove(from);}catch(...){std::error_code ignored;std::filesystem::remove(to,ignored);throw;}
    }
};
