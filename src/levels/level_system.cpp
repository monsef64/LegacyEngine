#include "levels/level_system.hpp"
#include "audio/audio_clip.hpp"
#include "core/worker_pool.hpp"
#include "prefabs/prefab.hpp"
#include "project/export_dependencies.hpp"

std::string LevelSystem::read(const std::filesystem::path& file) {
    if (std::filesystem::file_size(file) > ProjectIO::maxBytes) {
        throw std::runtime_error("Level exceeds 16 MiB");
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open level");
    }
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (text.size() > ProjectIO::maxBytes || in.bad()) {
        throw std::runtime_error("Cannot read level");
    }
    return text;
}

void LevelSystem::manifest() {
    Json json{{"version", 1}, {"levels", names_}, {"current", current_}};
    writeJsonFile(assets_.resolve("Levels/levels.json"), json);
}

void LevelSystem::cancel() {
    if (reader_.valid()) {
        reader_ = std::future<Transition>{};
    }
    pending_ = false;
    prepared_.reset();
    failure_.reset();
    bytes_.clear();
    requested_.clear();
}

bool LevelSystem::validName(const std::string& name) {
    if (name.empty() || name.size() > 128 || name == "." || name == ".." || name.back() == '.' ||
        name.back() == ' ') {
        return false;
    }
    for (unsigned char c : name) {
        if (c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

std::string LevelSystem::path(const std::string& name) {
    if (!validName(name)) {
        throw std::runtime_error("Invalid level name");
    }
    return "Levels/" + name + ".level";
}

void LevelSystem::setRoot(const std::filesystem::path& root) {
    if (root == assets_.root) {
        return;
    }
    cancel();
    assets_.root = root;
    names_.clear();
    current_.clear();
    error.clear();
    try {
        const auto file = assets_.resolve("Levels/levels.json");
        if (!std::filesystem::exists(file)) {
            return;
        }
        const auto json = readJsonFile(file, 128 * 1024);
        if (json.at("version") != 1 || !json.at("levels").is_array() ||
            json.at("levels").size() > 256) {
            throw std::runtime_error("Invalid level registry");
        }
        auto names = json.at("levels").get<std::vector<std::string>>();
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (!validName(names[i]) ||
                std::find(names.begin(), names.begin() + i, names[i]) != names.begin() + i) {
                throw std::runtime_error("Invalid or duplicate level name");
            }
        }
        auto current = json.at("current").get<std::string>();
        if (!current.empty() && std::find(names.begin(), names.end(), current) == names.end()) {
            throw std::runtime_error("Unknown current level");
        }
        names_ = std::move(names);
        current_ = std::move(current);
    } catch (const std::exception& e) {
        error = e.what();
    }
}

void LevelSystem::save(const std::string& name,
                       World& world,
                       std::string_view source,
                       const OrbitCamera& camera) {
    if (!validName(name)) {
        throw std::runtime_error("Use a level name without path separators");
    }
    bool added = std::find(names_.begin(), names_.end(), name) == names_.end();
    if (added && names_.size() >= 256) {
        throw std::runtime_error("Maximum 256 levels");
    }
    Terrains::saveAll(world, assets_);
    auto document = parseJson(ProjectIO::encode(world, source, camera, meshes_));
    ProjectIO::Loaded check;
    ProjectIO::decode(document.dump(), check, meshes_);
    writeJsonFile(assets_.resolve(path(name)), document);
    if (added) {
        names_.push_back(name);
    }
    manifest();
}

void LevelSystem::saveCurrent(World& world, std::string_view source, const OrbitCamera& camera) {
    const auto name = current_.empty() ? std::string("Main") : current_;
    save(name, world, source, camera);
    commit(name, true);
}

void LevelSystem::renameCurrent(const std::string& name) {
    if (pending_ || suspended || current_.empty()) {
        throw std::runtime_error("No editable current level");
    }
    if (!validName(name)) {
        throw std::runtime_error("Invalid level name");
    }
    if (name == current_) {
        return;
    }
    if (std::find(names_.begin(), names_.end(), name) != names_.end() ||
        std::filesystem::exists(assets_.resolve(path(name)))) {
        throw std::runtime_error("A level with that name exists");
    }
    auto it = std::find(names_.begin(), names_.end(), current_);
    const auto old = current_;
    std::filesystem::rename(assets_.resolve(path(old)), assets_.resolve(path(name)));
    *it = name;
    current_ = name;
    try {
        manifest();
    } catch (...) {
        *it = old;
        current_ = old;
        std::filesystem::rename(assets_.resolve(path(name)), assets_.resolve(path(old)));
        throw;
    }
}

void LevelSystem::create(const std::string& name) {
    if (std::find(names_.begin(), names_.end(), name) != names_.end() ||
        std::filesystem::exists(assets_.resolve(path(name)))) {
        throw std::runtime_error("A level with that name exists");
    }
    World empty;
    save(name, empty, "", OrbitCamera{});
}

bool LevelSystem::load(std::string name, bool async) {
    if (suspended || pending_) {
        error = "A level transition is already in progress";
        failure_ = Failure{name, error};
        return false;
    }
    if (name.empty()) {
        name = current_.empty() ? (names_.empty() ? "" : names_.front()) : current_;
    }
    if (std::find(names_.begin(), names_.end(), name) == names_.end()) {
        error = "Unknown level: " + name;
        failure_ = Failure{name, error};
        return false;
    }
    try {
        auto file = assets_.resolve(path(name));
        // Both APIs stage decoding/preloading off-thread. The ordinary API may
        // read its small level file now; neither publishes a partially loaded world.
        auto bytes = async ? std::string{} : read(file);
        auto job = WorkerPool::shared().submit(
            [assets = assets_, meshes = meshes_, name, file, bytes = std::move(bytes)]() mutable {
                if (bytes.empty()) {
                    bytes = read(file);
                }
                return prepare(assets, meshes, name, std::move(bytes));
            });
        if (!job) {
            throw std::runtime_error("Level preload worker queue is full");
        }
        reader_ = std::move(*job);
        requested_ = name;
        pending_ = true;
        preloadStarted_ = std::chrono::steady_clock::now();
        unload_ = false;
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        failure_ = Failure{name, error};
        return false;
    }
}

bool LevelSystem::unload(const std::string& name) {
    if (suspended || pending_ || current_.empty() || (!name.empty() && name != current_)) {
        error = "Only the current level can be unloaded";
        return false;
    }
    requested_.clear();
    bytes_.clear();
    unload_ = true;
    pending_ = true;
    error.clear();
    return true;
}

bool LevelSystem::relative(int offset) {
    auto it = std::find(names_.begin(), names_.end(), current_);
    if (it == names_.end()) {
        error = "No current level";
        return false;
    }
    auto index = std::distance(names_.begin(), it) + offset;
    if (index < 0 || index >= static_cast<std::ptrdiff_t>(names_.size())) {
        error = "No adjacent level";
        return false;
    }
    return load(names_[std::size_t(index)]);
}

std::optional<LevelSystem::Transition> LevelSystem::poll() {
    if (!pending_) {
        return {};
    }
    try {
        if (!unload_ &&
            std::chrono::steady_clock::now() - preloadStarted_ > std::chrono::seconds(120)) {
            throw std::runtime_error("Asset preloading timed out");
        }
        if (reader_.valid() &&
            reader_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return {};
        }
        if (reader_.valid()) {
            prepared_ = reader_.get();
        }
        if (unload_) {
            auto loaded = std::make_unique<ProjectIO::Loaded>();
            prepared_ = Transition{requested_, std::move(loaded)};
        }
        if (!prepared_) {
            return {};
        }
        if (preload && !unload_ && !preload(*prepared_)) {
            return {};
        }
        pending_ = false;
        error.clear();
        auto result = std::move(prepared_);
        prepared_.reset();
        return result;
    } catch (const std::exception& e) {
        pending_ = false;
        prepared_.reset();
        error = e.what();
        failure_ = Failure{requested_, error};
        return {};
    }
}

LevelSystem::Transition LevelSystem::prepare(AssetManager assets,
                                             ProjectIO::Meshes meshes,
                                             std::string name,
                                             std::string bytes) {
    Transition result;
    result.name = std::move(name);
    result.level = std::make_unique<ProjectIO::Loaded>();
    ProjectIO::decode(bytes, *result.level, meshes);
    Prefabs::validateLinks(assets, result.level->world, meshes);
    result.level->world.each<Terrain>([&](Entity, Terrain& t) {
        if (!t.asset.empty()) {
            Terrains::load(t, assets);
        }
    });
    // Serialize after prefab refresh so child assets and exposed overrides are included.
    const auto scene =
        ProjectIO::encode(result.level->world, result.level->source, result.level->camera, meshes);
    std::set<std::string> members;
    result.assets = GameExport::exportDependencies(assets, scene, "", false, &members);
    result.assets.insert(members.begin(), members.end());
    std::size_t audioBytes = 0;
    for (const auto& path : result.assets) {
        if (Prefabs::asset(path)) {
            (void)Prefabs::load(assets, path, meshes);
        }
        if (audioAsset(path)) {
            auto samples = decodeAudioClip(assets, path);
            audioBytes += samples.size() * sizeof(float);
            if (audioBytes > 256 * 1024 * 1024) {
                throw std::runtime_error("Level audio preload exceeds 256 MiB");
            }
            result.audio.emplace(path, std::move(samples));
        }
        if (animationAsset(path)) {
            if (result.clips.size() >= 64) {
                throw std::runtime_error("Level animation preload exceeds 64 clips");
            }
            result.clips.emplace(path, loadAnimationClip(assets, path));
        }
    }
    return result;
}

void LevelSystem::commit(const std::string& name, bool persist) {
    if (!name.empty() && std::find(names_.begin(), names_.end(), name) == names_.end()) {
        throw std::runtime_error("Unknown level");
    }
    current_ = name;
    if (persist) {
        manifest();
    }
}
