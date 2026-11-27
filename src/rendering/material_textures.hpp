#pragma once
#include "assets/assets.hpp"
#include <future>
#include <map>
#include <set>
class MaterialTextureCache {
    struct Entry {
        unsigned texture = 0;
        std::uint64_t generation = 0;
        std::size_t bytes = 0;
        std::uint64_t used = 0;
        std::filesystem::file_time_type stamp{};
        std::uintmax_t size = 0;
        std::chrono::steady_clock::time_point checked{};
    };
    std::map<std::string, std::pair<std::chrono::steady_clock::time_point, std::string>> failures_;
    std::map<std::string, Entry> entries_;
    struct Decoded {
        std::vector<unsigned char> pixels;
        int width = 0, height = 0;
        std::filesystem::file_time_type stamp{};
        std::uintmax_t size = 0;
        bool unchanged = false;
    };
    std::map<std::string, std::future<Decoded>> pending_;
    int quality_ = -1;
    unsigned uploads_ = 0;
    bool waiting_ = false;
    std::size_t bytes_ = 0;
    std::uint64_t frame_ = 0, generation_ = 0;

  public:
    ~MaterialTextureCache() {
        clear();
    }
    void clear();
    bool streaming() const {
        return waiting_ || !pending_.empty();
    }
    void beginFrame() {
        ++frame_;
        uploads_ = 0;
        waiting_ = false;
    }
    // Completed editor jobs otherwise occupy the bounded admission slots forever
    // when preloading never calls get() for their paths. WorkerPool futures can
    // be discarded without waiting; jobs own their inputs and no GPU resources.
    void discardUnrequested(const std::set<std::string>& paths) {
        for (auto it = pending_.begin(); it != pending_.end();) {
            const auto separator = it->first.rfind('|');
            if (it->first.substr(separator) != "|scaled" ||
                !paths.count(it->first.substr(0, separator))) {
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // Protect an entire preload working set before admitting new uploads. Without
    // this, path-order eviction can endlessly cycle through a set over budget.
    void retainForFrame(const std::string& path) {
        const auto it = entries_.find(path + "|scaled");
        if (it != entries_.end()) {
            it->second.used = frame_;
        }
    }
    std::uint64_t generation(const std::string& path, bool fullResolution = false) const {
        const auto it = entries_.find(path + (fullResolution ? "|full" : "|scaled"));
        return it == entries_.end() ? 0 : it->second.generation;
    }
    unsigned get(const AssetManager& assets,
                 const std::string& path,
                 std::string& error,
                 bool fullResolution = false);
};
