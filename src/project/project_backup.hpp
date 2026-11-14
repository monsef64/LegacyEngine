#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

// Portable ZIP (stored entries, UTF-8 names, streamed payloads). No shell tools
// or platform-specific archivers are required. Reject ZIP32 overflows explicitly.
inline std::filesystem::path backupProject(const std::filesystem::path& project) {
    namespace fs = std::filesystem;
    const auto root = fs::canonical(project).parent_path();
    const auto backups = root / "Backups";
    if (fs::is_symlink(backups)) {
        throw std::runtime_error("Backups must not be a symbolic link");
    }
    fs::create_directories(backups);
    const auto now = std::time(nullptr);
    const auto local = *std::localtime(&now);
    std::ostringstream stamp;
    stamp << std::put_time(&local, "%Y-%m-%d_%H-%M-%S");
    const auto base = project.stem().u8string() + "_Backup_" + stamp.str();
    auto output = backups / (base + ".zip");
    for (unsigned n = 2; fs::exists(output); ++n) {
        output = backups / (base + "_" + std::to_string(n) + ".zip");
    }
    const auto temporary = fs::path(output.u8string() + ".partial");
    struct Entry {
        fs::path path;
        std::string name;
        std::uint32_t size = 0, crc = 0, offset = 0;
        bool directory = false;
    };
    std::vector<Entry> entries;
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (it->path() == backups) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink()) {
            throw std::runtime_error("Backup does not follow symbolic links: " +
                                     it->path().u8string());
        }
        if (!it->is_directory() && !it->is_regular_file()) {
            throw std::runtime_error("Unsupported file in project backup");
        }
        Entry entry;
        entry.path = it->path();
        entry.directory = it->is_directory();
        entry.name =
            entry.path.lexically_relative(root).generic_u8string() + (entry.directory ? "/" : "");
        const auto bytes = entry.directory ? 0 : fs::file_size(entry.path);
        if (bytes >= 0xffffffffull || entry.name.size() > 65535 || entries.size() >= 65534) {
            throw std::runtime_error(
                "Project exceeds ZIP32 backup limits (4 GiB / 65,534 entries)");
        }
        entry.size = static_cast<std::uint32_t>(bytes);
        entries.push_back(std::move(entry));
    }
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        auto word = [&](std::uint32_t v, int bytes) {
            for (int i = 0; i < bytes; ++i) {
                out.put(static_cast<char>((v >> (8 * i)) & 255));
            }
        };
        auto position = [&]() {
            const auto p = out.tellp();
            if (p < 0 || static_cast<std::uint64_t>(p) >= 0xffffffffull) {
                throw std::runtime_error("Backup exceeds ZIP32's 4 GiB archive limit");
            }
            return static_cast<std::uint32_t>(p);
        };
        const auto time =
            std::uint32_t((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
        const auto date =
            std::uint32_t(((std::max(1980, std::min(2107, local.tm_year + 1900)) - 1980) << 9) |
                          ((local.tm_mon + 1) << 5) | local.tm_mday);
        std::array<char, 65536> buffer;
        for (auto& entry : entries) {
            entry.offset = position();
            word(0x04034b50, 4);
            word(20, 2);
            word(0x808, 2);
            word(0, 2);
            word(time, 2);
            word(date, 2);
            word(0, 4);
            word(0, 4);
            word(0, 4);
            word(entry.name.size(), 2);
            word(0, 2);
            out.write(entry.name.data(), entry.name.size());
            std::uint32_t crc = 0xffffffffu;
            if (!entry.directory) {
                std::ifstream in(entry.path, std::ios::binary);
                if (!in) {
                    throw std::runtime_error("Cannot read backup source: " + entry.name);
                }
                std::uint64_t copied = 0;
                while (in) {
                    in.read(buffer.data(), buffer.size());
                    const auto count = in.gcount();
                    copied += count;
                    if (copied > entry.size) {
                        throw std::runtime_error("File changed during backup: " + entry.name);
                    }
                    for (std::streamsize i = 0; i < count; ++i) {
                        crc ^= static_cast<unsigned char>(buffer[i]);
                        for (int bit = 0; bit < 8; ++bit) {
                            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
                        }
                    }
                    out.write(buffer.data(), count);
                }
                if (!in.eof() || copied != entry.size) {
                    throw std::runtime_error("File changed or unreadable during backup: " +
                                             entry.name);
                }
            }
            entry.crc = crc ^ 0xffffffffu;
            word(0x08074b50, 4);
            word(entry.crc, 4);
            word(entry.size, 4);
            word(entry.size, 4);
            position();
        }
        const auto directoryOffset = position();
        for (const auto& entry : entries) {
            word(0x02014b50, 4);
            word(20, 2);
            word(20, 2);
            word(0x808, 2);
            word(0, 2);
            word(time, 2);
            word(date, 2);
            word(entry.crc, 4);
            word(entry.size, 4);
            word(entry.size, 4);
            word(entry.name.size(), 2);
            word(0, 2);
            word(0, 2);
            word(0, 2);
            word(0, 2);
            word(entry.directory ? 0x10 : 0, 4);
            word(entry.offset, 4);
            out.write(entry.name.data(), entry.name.size());
        }
        const auto directorySize = position() - directoryOffset;
        word(0x06054b50, 4);
        word(0, 2);
        word(0, 2);
        word(entries.size(), 2);
        word(entries.size(), 2);
        word(directorySize, 4);
        word(directoryOffset, 4);
        word(0, 2);
        position();
        out.close();
        fs::rename(temporary, output);
        return output;
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
