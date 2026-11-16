#pragma once
#include "../../third_party/json/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
using Json = nlohmann::json;
// SIMD-accelerated parsing; serialization and the editable DOM retain Json.
Json parseJson(const std::string& text, std::size_t limit = 16 * 1024 * 1024);
Json readCompressedMesh(std::istream& stream, std::size_t limit, bool zstandard = false);
void writeCompressedMesh(std::ostream& stream,
                         const Json& document,
                         const char* magic = "LEMZ0002");
inline Json readJsonFile(const std::filesystem::path& path, std::size_t limit) {
    if (std::filesystem::file_size(path) > limit) {
        throw std::runtime_error("JSON file exceeds size limit");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot read JSON file");
    }
    char magic[8]{};
    in.read(magic, 8);
    if (in.gcount() == 8 &&
        (std::string(magic, 8) == "LEMZ0001" || std::string(magic, 8) == "LEMZ0002" ||
         std::string(magic, 8) == "TERZ0001")) {
        return readCompressedMesh(
            in, limit, magic[7] == '2' || std::string(magic, 8) == "TERZ0001");
    }
    in.clear();
    in.seekg(0);
    return parseJson({std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()}, limit);
}
inline void writeJsonFile(const std::filesystem::path& path, const Json& document) {
    std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        std::ofstream out(temp, std::ios::binary);
        if (path.extension() == ".terrain") {
            writeCompressedMesh(out, document, "TERZ0001");
        } else if (path.extension() == ".lemesh") {
            writeCompressedMesh(out, document);
        } else {
            out << document.dump(2) << '\n';
        }
        out.close();
        if (!out) {
            throw std::runtime_error("Cannot write JSON file");
        }
        std::filesystem::rename(temp, path);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}
