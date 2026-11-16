#pragma once
#include <filesystem>
#include <vector>
// Losslessly encode bottom-up BGR bytes as an ordinary RGB PNG; no quantization.
void writeBgrPng(const std::filesystem::path& file,
                 int width,
                 int height,
                 const std::vector<unsigned char>& pixels);
