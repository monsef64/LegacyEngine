#pragma once
#include "../../third_party/json/json.hpp"
#include "assets/subasset.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
using Json = nlohmann::json;
// SIMD-accelerated parsing; serialization and the editable DOM retain Json.
Json parseJson(const std::string& text, std::size_t limit = 16 * 1024 * 1024);
Json readCompressedMesh(std::istream& stream, std::size_t limit, bool zstandard = false);
void writeCompressedMesh(std::ostream& stream,
                         const Json& document,
                         const char* magic = "LEMZ0002");
// Shared immutable container snapshot avoids decompressing/parsing the complete
// model for each embedded material, image, animation and mesh request.
std::shared_ptr<const Json> readModelContainer(const std::filesystem::path& path);
void invalidateModelContainer();
inline Json readJsonFile(const std::filesystem::path& path, std::size_t limit) {
    if (embeddedAsset(path.generic_string())) {
        const auto root = readModelContainer(assetContainerPath(path));
        const auto& child = root->at("embeddedAssets").at(embeddedAssetName(path.generic_string()));
        if (child.dump().size() > limit) {
            throw std::runtime_error("Embedded JSON exceeds size limit");
        }
        return child;
    }
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
// Encoded image bytes from a regular asset or a bounded packed model member.
inline std::vector<unsigned char> readAssetBytes(const std::filesystem::path& path,
                                                 std::size_t limit) {
    if (embeddedAsset(path.generic_string())) {
        const auto doc = readJsonFile(path, limit * 2 + 1024);
        if (doc.value("format", "") != "LegacyEngineImage" || !doc.at("hex").is_string()) {
            throw std::runtime_error("Invalid packed image");
        }
        const auto& hex = doc.at("hex").get_ref<const std::string&>();
        if (hex.size() % 2 || hex.size() / 2 > limit) {
            throw std::runtime_error("Packed image exceeds limit");
        }
        auto digit = [](char c) -> unsigned {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            throw std::runtime_error("Invalid packed image encoding");
        };
        std::vector<unsigned char> bytes(hex.size() / 2);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = (digit(hex[i * 2]) << 4) | digit(hex[i * 2 + 1]);
        }
        return bytes;
    }
    const auto size = std::filesystem::file_size(path);
    if (size > limit) {
        throw std::runtime_error("Asset exceeds byte limit");
    }
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size));
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Cannot read asset bytes");
    }
    return bytes;
}
inline void writeJsonFile(const std::filesystem::path& path, const Json& document) {
    invalidateModelContainer();
    if (embeddedAsset(path.generic_string())) {
        const auto container = assetContainerPath(path);
        auto root = readJsonFile(container, 256 * 1024 * 1024);
        const auto member = embeddedAssetName(path.generic_string());
        if (!root.at("embeddedAssets").contains(member)) {
            throw std::runtime_error("Missing embedded asset");
        }
        root["embeddedAssets"][member] = document;
        if (root.dump().size() > 256 * 1024 * 1024) {
            throw std::runtime_error("Packed model exceeds size limit");
        }
        writeJsonFile(container, root);
        return;
    }
    std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        std::ofstream out(temp, std::ios::binary);
        if (path.extension() == ".terrain") {
            writeCompressedMesh(out, document, "TERZ0001");
        } else if ((path.extension() == ".mesh" || path.extension() == ".model" ||
                    path.extension() == ".lemesh")) {
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
