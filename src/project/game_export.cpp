#include "project/game_export.hpp"
#include "monocypher.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// BCrypt requires Windows types first; preserve this dependency order.
// clang-format off
#include <windows.h>
#include <bcrypt.h>
#include <winver.h>
// clang-format on
#else
#include <sys/stat.h>
#endif
namespace GameExport {
namespace {
constexpr std::size_t chunkSize = 16 * 1024 * 1024;
constexpr std::uint64_t maxTotal = 2ull * 1024 * 1024 * 1024;
struct Footer {
    unsigned char key[32], id[16];
    char magic[16];
};
static_assert(sizeof(Footer) == 64);
void randomBytes(void* output, std::size_t size) {
#ifdef _WIN32
    if (BCryptGenRandom(
            nullptr, static_cast<PUCHAR>(output), ULONG(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) !=
        0) {
        throw std::runtime_error("Operating system random generator failed");
    }
#else
    std::ifstream random("/dev/urandom", std::ios::binary);
    if (!random.read(static_cast<char*>(output), std::streamsize(size))) {
        throw std::runtime_error("Operating system random generator failed");
    }
#endif
}
std::string hex(const unsigned char* bytes, std::size_t size) {
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        result << std::setw(2) << unsigned(bytes[i]);
    }
    return result.str();
}
std::vector<unsigned char> readBytes(const std::filesystem::path& path, std::size_t limit) {
    if (std::filesystem::is_symlink(path)) {
        throw std::runtime_error("Symbolic links are not allowed in game packages");
    }
    const auto size = std::filesystem::file_size(path);
    if (size > limit) {
        throw std::runtime_error("Game package entry exceeds size limit");
    }
    std::vector<unsigned char> result(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(result.data()), result.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Cannot read game package entry");
    }
    return result;
}
void seal(const std::filesystem::path& path,
          const std::vector<unsigned char>& plain,
          const Footer& key,
          const std::string& label) {
    std::vector<unsigned char> encrypted(40 + plain.size());
    randomBytes(encrypted.data(), 24);
    const auto ad = label + "/" + hex(key.id, sizeof(key.id));
    crypto_aead_lock(encrypted.data() + 40,
                     encrypted.data() + 24,
                     key.key,
                     encrypted.data(),
                     reinterpret_cast<const unsigned char*>(ad.data()),
                     ad.size(),
                     plain.data(),
                     plain.size());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
    output.close();
    if (!output) {
        throw std::runtime_error("Cannot write encrypted game chunk");
    }
}
std::vector<unsigned char>
open(const std::filesystem::path& path, const Footer& key, const std::string& label) {
    auto encrypted = readBytes(path, chunkSize + 40);
    if (encrypted.size() < 40) {
        throw std::runtime_error("Truncated encrypted game chunk");
    }
    std::vector<unsigned char> plain(encrypted.size() - 40);
    const auto ad = label + "/" + hex(key.id, sizeof(key.id));
    if (crypto_aead_unlock(plain.data(),
                           encrypted.data() + 24,
                           key.key,
                           encrypted.data(),
                           reinterpret_cast<const unsigned char*>(ad.data()),
                           ad.size(),
                           encrypted.data() + 40,
                           plain.size()) != 0) {
        throw std::runtime_error("Game package authentication failed");
    }
    return plain;
}
std::string chunkName(std::size_t index) {
    std::ostringstream name;
    name << "chunk-" << std::setw(6) << std::setfill('0') << index << ".bin";
    return name.str();
}
void safeRelative(const std::string& name) {
    if (name.empty() || name.size() > 1024 ||
        name.find_first_of("\\:\0", 0, 3) != std::string::npos ||
        std::filesystem::path(name).is_absolute()) {
        throw std::runtime_error("Invalid package asset path");
    }
    for (const auto& part : std::filesystem::path(name)) {
        if (part == ".." || part == "." || part.empty()) {
            throw std::runtime_error("Invalid package asset path");
        }
    }
}
#ifdef _WIN32
void stamp(const std::filesystem::path& exe, const Settings& settings) {
    const auto ico = readBytes(settings.icon, 4 * 1024 * 1024);
    auto u16 = [&](std::size_t p) {
        if (p + 2 > ico.size()) {
            throw std::runtime_error("Invalid ICO");
        }
        return unsigned(ico[p]) | unsigned(ico[p + 1]) << 8;
    };
    auto u32 = [&](std::size_t p) {
        if (p + 4 > ico.size()) {
            throw std::runtime_error("Invalid ICO");
        }
        return std::uint32_t(ico[p]) | std::uint32_t(ico[p + 1]) << 8 |
               std::uint32_t(ico[p + 2]) << 16 | std::uint32_t(ico[p + 3]) << 24;
    };
    const unsigned count = u16(4);
    if (u16(0) != 0 || u16(2) != 1 || count == 0 || count > 32 || 6 + count * 16 > ico.size()) {
        throw std::runtime_error("Expected a valid multi-resolution .ico file");
    }
    HANDLE update = BeginUpdateResourceW(exe.c_str(), FALSE);
    if (!update) {
        throw std::runtime_error("Cannot edit Windows player resources");
    }
    try {
        std::vector<unsigned char> group(6 + count * 14);
        std::memcpy(group.data(), ico.data(), 6);
        for (unsigned i = 0; i < count; ++i) {
            const auto p = 6 + i * 16, size = u32(p + 8), offset = u32(p + 12);
            if (!size || std::uint64_t(offset) + size > ico.size()) {
                throw std::runtime_error("Invalid icon image range");
            }
            std::memcpy(group.data() + 6 + i * 14, ico.data() + p, 12);
            group[6 + i * 14 + 12] = static_cast<unsigned char>((i + 1) & 255);
            group[6 + i * 14 + 13] = static_cast<unsigned char>((i + 1) >> 8);
            if (!UpdateResourceW(update,
                                 MAKEINTRESOURCEW(3),
                                 MAKEINTRESOURCEW(i + 1),
                                 0x409,
                                 const_cast<unsigned char*>(ico.data() + offset),
                                 size)) {
                throw std::runtime_error("Cannot stamp player icon");
            }
        }
        if (!UpdateResourceW(update,
                             MAKEINTRESOURCEW(14),
                             MAKEINTRESOURCEW(1),
                             0x409,
                             group.data(),
                             DWORD(group.size()))) {
            throw std::runtime_error("Cannot stamp icon group");
        }
        auto wide = [](const std::string& text) {
            const int n =
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
            if (!n) {
                throw std::runtime_error("Invalid UTF-8 export metadata");
            }
            std::wstring value(std::size_t(n), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, value.data(), n);
            value.resize(n - 1);
            return value;
        };
        std::vector<unsigned char> version;
        auto word = [&](unsigned v) {
            version.push_back(v & 255);
            version.push_back((v >> 8) & 255);
        };
        auto align = [&] {
            while (version.size() % 4) {
                version.push_back(0);
            }
        };
        auto text = [&](const std::wstring& value) {
            for (wchar_t c : value) {
                word(unsigned(c));
            }
            word(0);
        };
        auto begin = [&](const std::wstring& key, unsigned valueLength, unsigned type) {
            const auto pos = version.size();
            word(0);
            word(valueLength);
            word(type);
            text(key);
            align();
            return pos;
        };
        auto end = [&](std::size_t pos) {
            const auto size = version.size() - pos;
            if (size > 65535) {
                throw std::runtime_error("Version resource too large");
            }
            version[pos] = size & 255;
            version[pos + 1] = (size >> 8) & 255;
        };
        auto root = begin(L"VS_VERSION_INFO", sizeof(VS_FIXEDFILEINFO), 0);
        unsigned a = 0, b = 0, c = 0;
        std::sscanf(settings.version.c_str(), "%u.%u.%u", &a, &b, &c);
        VS_FIXEDFILEINFO fixed{};
        fixed.dwSignature = 0xfeef04bd;
        fixed.dwStrucVersion = 0x10000;
        fixed.dwFileVersionMS = (a << 16) | b;
        fixed.dwFileVersionLS = c << 16;
        fixed.dwProductVersionMS = fixed.dwFileVersionMS;
        fixed.dwProductVersionLS = fixed.dwFileVersionLS;
        fixed.dwFileFlagsMask = 0x3f;
        fixed.dwFileOS = VOS_NT_WINDOWS32;
        fixed.dwFileType = VFT_APP;
        const auto* raw = reinterpret_cast<const unsigned char*>(&fixed);
        version.insert(version.end(), raw, raw + sizeof(fixed));
        align();
        const auto strings = begin(L"StringFileInfo", 0, 1), table = begin(L"040904B0", 0, 1);
        const std::pair<const wchar_t*, std::string> values[]{
            {L"CompanyName", settings.company},
            {L"ProductName", settings.name},
            {L"FileDescription", settings.description},
            {L"FileVersion", settings.version},
            {L"ProductVersion", settings.version},
            {L"LegalCopyright", settings.copyright},
            {L"OriginalFilename", settings.name + ".exe"}};
        for (const auto& item : values) {
            const auto value = wide(item.second);
            const auto pos = begin(item.first, unsigned(value.size() + 1), 1);
            text(value);
            end(pos);
            align();
        }
        end(table);
        end(strings);
        const auto vars = begin(L"VarFileInfo", 0, 1), translation = begin(L"Translation", 4, 0);
        word(0x409);
        word(1200);
        end(translation);
        end(vars);
        end(root);
        if (!UpdateResourceW(update,
                             MAKEINTRESOURCEW(16),
                             MAKEINTRESOURCEW(1),
                             0x409,
                             version.data(),
                             DWORD(version.size()))) {
            throw std::runtime_error("Cannot stamp player version metadata");
        }
        if (!EndUpdateResourceW(update, FALSE)) {
            update = nullptr;
            throw std::runtime_error("Cannot commit player resources");
        }
        update = nullptr;
    } catch (...) {
        if (update) {
            EndUpdateResourceW(update, TRUE);
        }
        throw;
    }
}
#endif
} // namespace
void validate(const Settings& s) {
    if (s.name.empty() || s.name.size() > 64 || s.name.front() == ' ' || s.name.back() == ' ' ||
        s.name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-") !=
            std::string::npos) {
        throw std::runtime_error("Game Name must be 1..64 letters, numbers, spaces, '_' or '-'");
    }
    std::string lower = s.name;
    for (auto& c : lower) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul" || lower == "com1" ||
        lower == "lpt1" ||
        (lower.size() == 4 && (lower.substr(0, 3) == "com" || lower.substr(0, 3) == "lpt") &&
         lower[3] >= '1' && lower[3] <= '9')) {
        throw std::runtime_error("Reserved executable name");
    }
    if (s.company.empty() || s.description.empty() || s.copyright.empty() ||
        s.company.size() > 256 || s.description.size() > 1024 || s.copyright.size() > 1024) {
        throw std::runtime_error("Company, Description and Copyright are required (bounded text)");
    }
    unsigned a = 0, b = 0, c = 0;
    char tail = 0;
    if (std::sscanf(s.version.c_str(), "%u.%u.%u%c", &a, &b, &c, &tail) != 3 || a > 65535 ||
        b > 65535 || c > 65535) {
        throw std::runtime_error("Version must be major.minor.patch, each 0..65535");
    }
    if (s.output.empty() || s.player.empty() || s.icon.empty()) {
        throw std::runtime_error("Output folder, player template and icon are required");
    }
    if (!std::filesystem::is_regular_file(s.player) || std::filesystem::is_symlink(s.player)) {
        throw std::runtime_error("Build/install the native LegacyPlayer template first");
    }
    {
        std::ifstream player(s.player, std::ios::binary);
        char marker[32]{};
        player.seekg(-32, std::ios::end);
        player.read(marker, sizeof(marker));
        if (!player || std::memcmp(marker, "LEGACY_PLAYER_TEMPLATE_000000001", 32) != 0) {
            throw std::runtime_error(
                "Expected a built LegacyPlayer template, not an editor or previously exported game");
        }
        player.clear();
        player.seekg(0);
        char format[4]{};
        player.read(format, sizeof(format));
#ifdef _WIN32
        if (format[0] != 'M' || format[1] != 'Z') {
            throw std::runtime_error("Windows export requires a Windows player template");
        }
#else
        if (std::memcmp(format, "\177ELF", 4) != 0) {
            throw std::runtime_error("Linux export requires a Linux player template");
        }
#endif
    }
    if (!std::filesystem::is_regular_file(s.icon) || std::filesystem::is_symlink(s.icon) ||
        std::filesystem::file_size(s.icon) > 4 * 1024 * 1024) {
        throw std::runtime_error("Icon must be a regular file up to 4 MiB");
    }
#ifdef _WIN32
    if (AssetManager::extension(s.icon) != ".ico") {
        throw std::runtime_error("Windows export requires an .ico icon");
    }
#else
    if (AssetManager::extension(s.icon) != ".png") {
        throw std::runtime_error("Linux export requires a .png icon");
    }
#endif
}
void build(const Settings& s,
           const AssetManager& assets,
           const std::string& scene,
           const std::filesystem::path& engineData) {
    validate(s);
    const auto destination = std::filesystem::absolute(s.output).lexically_normal();
    if (std::filesystem::exists(destination) || std::filesystem::is_symlink(destination)) {
        throw std::runtime_error(
            "Choose a new output folder; existing exports are never overwritten");
    }
    const auto source = std::filesystem::weakly_canonical(assets.root),
               output = std::filesystem::weakly_canonical(destination);
    const auto inside = output.lexically_relative(source),
               ancestor = source.lexically_relative(output);
    if (inside.empty() || *inside.begin() != ".." || ancestor.empty() ||
        *ancestor.begin() != "..") {
        throw std::runtime_error("Export must be outside the source asset tree");
    }
    Footer footer{};
    randomBytes(footer.key, 32);
    randomBytes(footer.id, 16);
    std::memcpy(footer.magic, "LEGACYPLAYER0001", 16);
    auto staging = destination;
    staging += ".building-" + hex(footer.id, 8);
    std::filesystem::create_directories(staging.parent_path());
    if (!std::filesystem::create_directory(staging)) {
        throw std::runtime_error("Cannot create private export staging folder");
    }
    try {
        const auto data = staging / (s.name + "_Data");
        std::filesystem::create_directory(data);
        struct File {
            std::filesystem::path source;
            std::string name, memory;
            bool inMemory = false;
        };
        std::vector<File> files;
        files.push_back({{}, "Game.legacy", scene, true});
        auto addTree = [&](const std::filesystem::path& root, const std::string& prefix) {
            if (!std::filesystem::exists(root)) {
                return;
            }
            for (auto it = std::filesystem::recursive_directory_iterator(root);
                 it != std::filesystem::recursive_directory_iterator();
                 ++it) {
                if (it->is_symlink()) {
                    throw std::runtime_error("Export source contains symbolic links");
                }
                if (it->is_directory() && it->path().filename() == ".cache") {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file()) {
                    if (files.size() >= 20000) {
                        throw std::runtime_error("Export exceeds 20000 files");
                    }
                    const auto name =
                        prefix + "/" + it->path().lexically_relative(root).generic_string();
                    safeRelative(name);
                    files.push_back({it->path(), name, {}, false});
                }
            }
        };
        addTree(assets.root, "Assets");
        const auto project =
            assets.root.filename() == "Assets" ? assets.root.parent_path() : assets.root;
        addTree(project / "Settings", "Settings");
        if (!std::filesystem::is_directory(engineData / "shaders")) {
            throw std::runtime_error("Player engine data/shaders directory is missing");
        }
        addTree(engineData, "Engine/data");
        // License notices are deliberately readable, unlike game asset chunks.
        {
            std::ofstream notices(data / "LICENSES.txt", std::ios::binary);
            std::size_t totalNotices = 0;
            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(engineData / "license")) {
                if (!entry.is_regular_file() || entry.is_symlink()) {
                    continue;
                }
                const auto bytes = readBytes(entry.path(), 2 * 1024 * 1024);
                if ((totalNotices += bytes.size()) > 16 * 1024 * 1024) {
                    throw std::runtime_error("License notices exceed limit");
                }
                notices << "\n--- "
                        << entry.path().lexically_relative(engineData / "license").generic_string()
                        << " ---\n";
                notices.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
            notices.close();
            if (!notices) {
                throw std::runtime_error("Cannot write required license notices");
            }
        }
        files.push_back({s.icon, "Icon" + AssetManager::extension(s.icon), {}, false});
        Json manifest{{"format", "LegacyGame1"},
                      {"name", s.name},
                      {"company", s.company},
                      {"startLevel", s.startLevel},
                      {"version", s.version},
                      {"description", s.description},
                      {"copyright", s.copyright},
                      {"icon", "Icon" + AssetManager::extension(s.icon)},
                      {"files", Json::array()}};
        std::vector<unsigned char> buffer;
        buffer.reserve(chunkSize);
        std::uint64_t total = 0;
        std::size_t chunks = 0;
        auto append = [&](const unsigned char* bytes, std::size_t size) {
            while (size) {
                const auto count = std::min(size, chunkSize - buffer.size());
                buffer.insert(buffer.end(), bytes, bytes + count);
                bytes += count;
                size -= count;
                if (buffer.size() == chunkSize) {
                    seal(data / chunkName(chunks),
                         buffer,
                         footer,
                         "chunk/" + std::to_string(chunks));
                    ++chunks;
                    buffer.clear();
                }
            }
        };
        std::set<std::string> names;
        for (const auto& file : files) {
            safeRelative(file.name);
            std::string normalized = file.name;
            for (auto& c : normalized) {
                c = char(std::tolower(static_cast<unsigned char>(c)));
            }
            if (!names.insert(normalized).second) {
                throw std::runtime_error("Case-conflicting exported asset path");
            }
            const auto size =
                file.inMemory ? file.memory.size() : std::filesystem::file_size(file.source);
            if (size > 256 * 1024 * 1024 || size > maxTotal - total) {
                throw std::runtime_error(
                    "Export exceeds per-file/total size limits (256 MiB/2 GiB)");
            }
            total += size;
            manifest["files"].push_back({{"path", file.name}, {"size", size}});
            if (file.inMemory) {
                append(reinterpret_cast<const unsigned char*>(file.memory.data()),
                       file.memory.size());
            } else {
                const auto stamp = std::filesystem::last_write_time(file.source);
                std::ifstream input(file.source, std::ios::binary);
                std::array<unsigned char, 65536> bytes{};
                std::uint64_t read = 0;
                while (input) {
                    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
                    auto n = input.gcount();
                    read += n;
                    append(bytes.data(), std::size_t(n));
                }
                if (!input.eof() || read != size ||
                    std::filesystem::last_write_time(file.source) != stamp ||
                    std::filesystem::file_size(file.source) != size) {
                    throw std::runtime_error("Source changed or could not be read during export");
                }
            }
        }
        if (!buffer.empty()) {
            seal(data / chunkName(chunks), buffer, footer, "chunk/" + std::to_string(chunks));
            ++chunks;
        }
        manifest["chunks"] = chunks;
        manifest["total"] = total;
        const auto json = manifest.dump();
        if (json.size() > chunkSize) {
            throw std::runtime_error("Export manifest exceeds size limit");
        }
        seal(data / "manifest.bin",
             std::vector<unsigned char>(json.begin(), json.end()),
             footer,
             "manifest");
#ifdef _WIN32
        const auto executable = staging / (s.name + ".exe");
#else
        const auto executable = staging / s.name;
#endif
        std::filesystem::copy_file(s.player, executable);
#ifdef _WIN32
        stamp(executable, s);
        for (const auto& file :
             std::filesystem::directory_iterator(std::filesystem::path(s.player).parent_path())) {
            if (file.is_regular_file() && !file.is_symlink() &&
                AssetManager::extension(file.path()) == ".dll") {
                std::filesystem::copy_file(file.path(), staging / file.path().filename());
            }
        }
#else
        std::filesystem::permissions(executable,
                                     std::filesystem::perms::owner_exec |
                                         std::filesystem::perms::group_exec |
                                         std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::add);
#endif
        std::ofstream exe(executable, std::ios::binary | std::ios::app);
        exe.write(reinterpret_cast<const char*>(&footer), sizeof(footer));
        exe.close();
        if (!exe) {
            throw std::runtime_error("Cannot configure exported player");
        }
        std::filesystem::rename(staging, destination);
        crypto_wipe(&footer, sizeof(footer));
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(staging, ec);
        crypto_wipe(&footer, sizeof(footer));
        throw;
    }
}
Mounted::~Mounted() {
    if (!root.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
}
void mount(const std::filesystem::path& executable, Mounted& mounted) {
    Footer footer{};
    std::ifstream exe(executable, std::ios::binary);
    exe.seekg(-std::streamoff(sizeof(footer)), std::ios::end);
    exe.read(reinterpret_cast<char*>(&footer), sizeof(footer));
    if (!exe || std::memcmp(footer.magic, "LEGACYPLAYER0001", 16) != 0) {
        throw std::runtime_error("This player template is not exported/configured");
    }
    struct Wipe {
        Footer& footer;
        ~Wipe() {
            crypto_wipe(&footer, sizeof(footer));
        }
    } wipe{footer};
    const auto data = executable.parent_path() / (executable.stem().string() + "_Data");
    if (std::filesystem::is_symlink(data)) {
        throw std::runtime_error("Invalid game data directory");
    }
    const auto plain = open(data / "manifest.bin", footer, "manifest");
    const auto meta = parseJson(std::string(plain.begin(), plain.end()), chunkSize);
    if (meta.at("format") != "LegacyGame1" || !meta.at("files").is_array() ||
        meta.at("files").size() > 20000) {
        throw std::runtime_error("Invalid game manifest");
    }
    const auto chunks = meta.at("chunks").get<std::uint64_t>(),
               total = meta.at("total").get<std::uint64_t>();
    if (total > maxTotal || chunks != (total + chunkSize - 1) / chunkSize) {
        throw std::runtime_error("Invalid chunk count/size");
    }
    std::set<std::string> names;
    std::uint64_t sum = 0;
    for (const auto& file : meta.at("files")) {
        const auto name = file.at("path").get<std::string>();
        safeRelative(name);
        std::string normalized = name;
        for (auto& c : normalized) {
            c = char(std::tolower(static_cast<unsigned char>(c)));
        }
        if (!names.insert(normalized).second) {
            throw std::runtime_error("Duplicate package path");
        }
        const auto size = file.at("size").get<std::uint64_t>();
        if (size > 256 * 1024 * 1024 || size > maxTotal - sum) {
            throw std::runtime_error("Invalid file length");
        }
        sum += size;
    }
    if (sum != total) {
        throw std::runtime_error("Invalid package total");
    }
    unsigned char random[16];
    randomBytes(random, sizeof(random));
    const auto temp =
        std::filesystem::temp_directory_path() / ("LegacyGame-" + hex(random, sizeof(random)));
#ifdef _WIN32
    if (!std::filesystem::create_directory(temp)) {
#else
    if (::mkdir(temp.c_str(), 0700) != 0) {
#endif
        throw std::runtime_error("Cannot create private runtime directory");
    }
    mounted.root = temp;
    std::filesystem::permissions(
        temp, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
    std::size_t chunk = 0, offset = 0;
    std::vector<unsigned char> bytes;
    for (const auto& file : meta.at("files")) {
        const auto path = temp / file.at("path").get<std::string>();
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        auto remaining = file.at("size").get<std::uint64_t>();
        while (remaining) {
            if (offset == bytes.size()) {
                if (chunk >= chunks) {
                    throw std::runtime_error("Missing game chunk");
                }
                bytes = open(data / chunkName(chunk), footer, "chunk/" + std::to_string(chunk));
                ++chunk;
                offset = 0;
                const auto expected =
                    std::min<std::uint64_t>(chunkSize, total - (chunk - 1) * chunkSize);
                if (bytes.size() != expected) {
                    throw std::runtime_error("Invalid chunk length");
                }
            }
            const auto count = std::min<std::uint64_t>(remaining, bytes.size() - offset);
            output.write(reinterpret_cast<const char*>(bytes.data() + offset),
                         std::streamsize(count));
            offset += count;
            remaining -= count;
        }
        output.close();
        if (!output) {
            throw std::runtime_error("Cannot materialize runtime asset");
        }
    }
    mounted.metadata = meta;
}
} // namespace GameExport
