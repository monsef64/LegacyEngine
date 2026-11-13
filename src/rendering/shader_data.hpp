#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
inline std::string loadShaderData(const char* name) {
    const char* base = SDL_GetBasePath();
    if (!base) {
        throw std::runtime_error("Cannot locate engine data directory");
    }
    const auto path = std::filesystem::path(base) / "data" / "shaders" / name;
    const auto bytes = std::filesystem::file_size(path);
    if (bytes > 256 * 1024) {
        throw std::runtime_error("Shader exceeds 256 KiB: " + path.string());
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot read shader: " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
