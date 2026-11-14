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

// Mesh envelope: magic, little-endian CBOR byte count, then a zlib stream.
// Limits are applied before allocation and checked again after inflation.
#include <zlib.h>
Json readCompressedMesh(std::istream& stream, std::size_t limit) {
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
    uLongf length = uLongf(count);
    if (uncompress(plain.data(), &length, packed.data(), uLong(packed.size())) != Z_OK ||
        length != count) {
        throw std::runtime_error("Corrupt compressed mesh");
    }
    return Json::from_cbor(plain, true, true);
}
void writeCompressedMesh(std::ostream& stream, const Json& document) {
    auto plain = Json::to_cbor(document);
    if (plain.size() > 256ull * 1024 * 1024) {
        throw std::runtime_error("Expanded mesh exceeds 256 MiB");
    }
    uLongf count = compressBound(uLong(plain.size()));
    std::vector<unsigned char> packed(count);
    if (compress2(packed.data(), &count, plain.data(), uLong(plain.size()), Z_BEST_SPEED) != Z_OK) {
        throw std::runtime_error("Mesh compression failed");
    }
    stream.write("LEMZ0001", 8);
    unsigned char size[8]{};
    for (int i = 0; i < 8; ++i) {
        size[i] = static_cast<unsigned char>(std::uint64_t(plain.size()) >> (8 * i));
    }
    stream.write(reinterpret_cast<const char*>(size), 8);
    stream.write(reinterpret_cast<const char*>(packed.data()), std::streamsize(count));
}
