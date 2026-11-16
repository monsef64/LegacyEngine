#include "assets/image_write.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <zlib.h>
namespace {
void big(std::ostream& out, std::uint32_t x) {
    for (int shift : {24, 16, 8, 0}) {
        out.put(char(x >> shift));
    }
}
void chunk(std::ostream& out, const char* tag, const unsigned char* bytes, std::size_t size) {
    big(out, std::uint32_t(size));
    out.write(tag, 4);
    if (size) {
        out.write(reinterpret_cast<const char*>(bytes), std::streamsize(size));
    }
    auto crc = crc32(0, reinterpret_cast<const Bytef*>(tag), 4);
    if (size) {
        crc = crc32(crc, bytes, uInt(size));
    }
    big(out, std::uint32_t(crc));
}
} // namespace
void writeBgrPng(const std::filesystem::path& file,
                 int width,
                 int height,
                 const std::vector<unsigned char>& pixels) {
    if (width < 1 || height < 1 || width > 4096 || height > 4096 ||
        pixels.size() != std::size_t(width) * height * 3) {
        throw std::runtime_error("Invalid PNG dimensions/pixels");
    }
    const std::size_t stride = std::size_t(width) * 3;
    std::vector<unsigned char> filtered((stride + 1) * height), row(stride), previous(stride, 0),
        candidate(stride), best(stride);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < 3; ++c) {
                row[x * 3 + c] = pixels[(std::size_t(height - 1 - y) * width + x) * 3 + 2 - c];
            }
        }
        unsigned bestScore = std::numeric_limits<unsigned>::max();
        int filter = 0;
        for (int type = 0; type <= 2; ++type) {
            unsigned score = 0;
            for (std::size_t x = 0; x < stride; ++x) {
                const int predict = type == 1   ? (x >= 3 ? row[x - 3] : 0)
                                    : type == 2 ? previous[x]
                                                : 0;
                candidate[x] = static_cast<unsigned char>(int(row[x]) - predict);
                score += std::min(unsigned(candidate[x]), 256u - unsigned(candidate[x]));
            }
            if (score < bestScore) {
                bestScore = score;
                filter = type;
                best = candidate;
            }
        }
        filtered[std::size_t(y) * (stride + 1)] = static_cast<unsigned char>(filter);
        std::copy(best.begin(), best.end(), filtered.begin() + std::size_t(y) * (stride + 1) + 1);
        previous = row;
    }
    uLongf size = compressBound(uLong(filtered.size()));
    std::vector<unsigned char> compressed(size);
    if (compress2(compressed.data(),
                  &size,
                  filtered.data(),
                  uLong(filtered.size()),
                  Z_BEST_COMPRESSION) != Z_OK) {
        throw std::runtime_error("PNG compression failed");
    }
    std::ofstream out(file, std::ios::binary);
    const unsigned char signature[]{137, 80, 78, 71, 13, 10, 26, 10};
    out.write(reinterpret_cast<const char*>(signature), 8);
    unsigned char ihdr[13]{};
    for (int i = 0; i < 4; ++i) {
        ihdr[i] = static_cast<unsigned char>(unsigned(width) >> (24 - i * 8));
        ihdr[4 + i] = static_cast<unsigned char>(unsigned(height) >> (24 - i * 8));
    }
    ihdr[8] = 8;
    ihdr[9] = 2;
    chunk(out, "IHDR", ihdr, 13);
    chunk(out, "IDAT", compressed.data(), size);
    chunk(out, "IEND", nullptr, 0);
    out.close();
    if (!out) {
        throw std::runtime_error("Cannot write PNG image");
    }
}
