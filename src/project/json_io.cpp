#include "project/json_io.hpp"
#include <simdjson.h>
namespace {
Json convert(simdjson::dom::element element, int depth) {
    if (depth > 64) {
        throw std::runtime_error("JSON nesting exceeds limit");
    }
    using simdjson::dom::element_type;
    switch (element.type()) {
    case element_type::ARRAY: {
        Json result = Json::array();
        for (auto value : element.get_array()) {
            result.push_back(convert(value, depth + 1));
        }
        return result;
    }
    case element_type::OBJECT: {
        Json result = Json::object();
        for (auto field : element.get_object()) {
            result[std::string(field.key)] = convert(field.value, depth + 1);
        }
        return result;
    }
    case element_type::INT64:
        return std::int64_t(element.get_int64());
    case element_type::UINT64:
        return std::uint64_t(element.get_uint64());
    case element_type::DOUBLE:
        return double(element.get_double());
    case element_type::STRING:
        return std::string(std::string_view(element.get_string()));
    case element_type::BOOL:
        return bool(element.get_bool());
    case element_type::NULL_VALUE:
        return nullptr;
    }
    throw std::runtime_error("Unsupported JSON element");
}
} // namespace
Json parseJson(const std::string& text, std::size_t limit) {
    if (text.size() > limit) {
        throw std::runtime_error("JSON exceeds configured size limit");
    }
    try {
        // Grow on demand: file readers supply their own limit (meshes allow 256 MiB).
        thread_local simdjson::dom::parser parser;
        auto document = parser.parse(text);
        return convert(document.value(), 0);
    } catch (const simdjson::simdjson_error& e) {
        throw std::runtime_error(std::string("Invalid JSON: ") + e.what());
    }
}

// Mesh envelope: magic, little-endian CBOR byte count, then a compressed stream (v1 zlib, v2
// Zstandard). Limits are applied before allocation and checked again after inflation.
#include <zlib.h>
#include <zstd.h>
Json readCompressedMesh(std::istream& stream, std::size_t limit, bool zstandard) {
    unsigned char size[8]{};
    stream.read(reinterpret_cast<char*>(size), 8);
    if (!stream) {
        throw std::runtime_error("Truncated compressed mesh header");
    }
    std::uint64_t count = 0;
    for (int i = 0; i < 8; ++i) {
        count |= std::uint64_t(size[i]) << (8 * i);
    }
    if (!count || count > limit) {
        throw std::runtime_error("Expanded mesh exceeds size limit");
    }
    std::vector<unsigned char> packed{std::istreambuf_iterator<char>(stream),
                                      std::istreambuf_iterator<char>()};
    if (packed.empty() || packed.size() > limit) {
        throw std::runtime_error("Invalid compressed mesh size");
    }
    std::vector<unsigned char> plain(std::size_t(count), 0);
    if (zstandard) {
        const auto length =
            ZSTD_decompress(plain.data(), plain.size(), packed.data(), packed.size());
        if (ZSTD_isError(length) || length != count) {
            throw std::runtime_error("Corrupt Zstandard mesh");
        }
        return Json::from_cbor(plain, true, true);
    }
    uLongf length = uLongf(count);
    if (uncompress(plain.data(), &length, packed.data(), uLong(packed.size())) != Z_OK ||
        length != count) {
        throw std::runtime_error("Corrupt compressed mesh");
    }
    return Json::from_cbor(plain, true, true);
}
// One retained container across all workers, rather than one large DOM per
// embedded member or per thread. Callers keep immutable snapshots while in use.
#include <atomic>
#include <mutex>
namespace {
std::atomic<std::uint64_t> containerGeneration{0};
}
void invalidateModelContainer() {
    ++containerGeneration;
}
std::shared_ptr<const Json> readModelContainer(const std::filesystem::path& path) {
    static std::mutex mutex;
    static std::filesystem::path key;
    static std::filesystem::file_time_type modified;
    static std::uintmax_t bytes = 0;
    static std::uint64_t generation = 0;
    static std::shared_ptr<const Json> cached;
    const auto file = std::filesystem::absolute(path).lexically_normal();
    const auto stamp = std::filesystem::last_write_time(file);
    const auto size = std::filesystem::file_size(file);
    const auto version = containerGeneration.load();
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (cached && key == file && modified == stamp && bytes == size && generation == version) {
            return cached;
        }
    }
    auto document = std::make_shared<const Json>(readJsonFile(file, 256 * 1024 * 1024));
    // Large containers are still readable, but don't retain an unbounded DOM.
    // The import format's decoded byte limit remains enforced by readJsonFile.
    std::uint64_t decodedBytes = size;
    {
        std::ifstream input(file, std::ios::binary);
        unsigned char header[16]{};
        input.read(reinterpret_cast<char*>(header), sizeof(header));
        if (input.gcount() == 16 &&
            (std::string(reinterpret_cast<char*>(header), 8) == "LEMZ0001" ||
             std::string(reinterpret_cast<char*>(header), 8) == "LEMZ0002")) {
            decodedBytes = 0;
            for (int i = 0; i < 8; ++i) {
                decodedBytes |= std::uint64_t(header[8 + i]) << (8 * i);
            }
        }
    }
    const bool retain = decodedBytes <= 32 * 1024 * 1024;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (retain && version == containerGeneration.load() &&
            std::filesystem::last_write_time(file) == stamp &&
            std::filesystem::file_size(file) == size) {
            cached = document;
            key = file;
            modified = stamp;
            bytes = size;
            generation = version;
        }
    }
    return document;
}
