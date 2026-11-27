#pragma once
#include "animation/animation_clip.hpp"
#include "assets/assets.hpp"
#include "project/project_io.hpp"
#include <algorithm>
#include <future>
#include <optional>

// Single active level. Requests never replace a World inside a Lua callback.
// Workers decode private Worlds and dependencies; publication/GL uploads stay on the main thread.
class LevelSystem {
  public:
    inline static LevelSystem* active = nullptr;
    struct Transition {
        std::string name;
        std::unique_ptr<ProjectIO::Loaded> level;
        std::set<std::string> assets;
        std::map<std::string, std::vector<float>> audio;
        std::map<std::string, AnimationClip> clips;
    };
    struct Failure {
        std::string name, error;
    };
    std::optional<Failure> takeFailure() {
        auto result = std::move(failure_);
        failure_.reset();
        return result;
    }
    // Render-thread gate: return true only after bounded resource uploads finish.
    std::function<bool(Transition&)> preload;
    std::string error;
    bool suspended = false;

  private:
    std::optional<Failure> failure_;
    AssetManager assets_;
    ProjectIO::Meshes meshes_;
    std::vector<std::string> names_;
    std::string current_, requested_, bytes_;
    bool pending_ = false, unload_ = false;
    std::chrono::steady_clock::time_point preloadStarted_{};
    std::future<Transition> reader_;
    std::optional<Transition> prepared_;

    static std::string read(const std::filesystem::path& file);
    void manifest();

  public:
    static Transition
    prepare(AssetManager assets, ProjectIO::Meshes meshes, std::string name, std::string bytes);
    explicit LevelSystem(const ProjectIO::Meshes& meshes) : meshes_(meshes) {}
    ~LevelSystem() {
        if (active == this) {
            active = nullptr;
        }
    }
    const std::string& current() const {
        return current_;
    }
    const std::vector<std::string>& names() const {
        return names_;
    }
    bool busy() const {
        return pending_;
    }
    void cancel();
    const auto& root() const {
        return assets_.root;
    }
    static bool validName(const std::string& name);
    static std::string path(const std::string& name);
    void setRoot(const std::filesystem::path& root);
    void
    save(const std::string& name, World& world, std::string_view source, const OrbitCamera& camera);
    void saveCurrent(World& world, std::string_view source, const OrbitCamera& camera);
    void renameCurrent(const std::string& name);
    void create(const std::string& name);
    bool load(std::string name = {}, bool async = false);
    bool unload(const std::string& name = {});
    bool relative(int offset);
    std::optional<Transition> poll();
    void commit(const std::string& name, bool persist);
};
