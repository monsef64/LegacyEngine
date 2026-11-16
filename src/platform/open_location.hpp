#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <stdexcept>
#include <string>

// Launch the desktop file manager without shell interpolation. Assets open their
// containing folder; directories open themselves. Never create missing paths.
inline void openLocation(const std::filesystem::path& path) {
    auto folder = std::filesystem::absolute(path);
    if (!std::filesystem::exists(folder)) {
        throw std::runtime_error("Location no longer exists");
    }
    if (!std::filesystem::is_directory(folder)) {
        folder = folder.parent_path();
    }
    auto raw = folder.generic_u8string();
    std::string encoded;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : raw) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += char(c);
        } else {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 15];
        }
    }
    std::string uri;
    if (encoded.rfind("//", 0) == 0) {
        uri = "file:" + encoded; // UNC share
    } else {
        uri = (encoded.front() == '/' ? "file://" : "file:///") + encoded;
    }
    if (!SDL_OpenURL(uri.c_str())) {
        throw std::runtime_error(std::string("Cannot open location: ") + SDL_GetError());
    }
}
