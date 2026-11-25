#pragma once
#include "animation/animation_property.hpp"
#include "assets/assets.hpp"
#include "core/hierarchy.hpp"
#include "models/model_asset.hpp"
#include "project/json_io.hpp"
#include <mutex>
#include <set>

struct AnimationKey {
    float time = 0;
    AnimationValue value{};
};
struct AnimationTrack {
    std::string target, property = "Transform.position";
    std::vector<AnimationKey> keys;
};
struct AnimationScriptEvent {
    float time = 0;
    std::string target, script, function;
};
inline bool animationFunctionName(const std::string& name) {
    if (name.empty() || name.size() > 128 || name[0] == '_' ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
            std::string::npos ||
        std::isdigit(static_cast<unsigned char>(name[0]))) {
        return false;
    }
    return name != "onGUI" && name != "onStart" && name != "onUpdate" && name != "onFixedUpdate" &&
           name != "onDestroy";
}
struct AnimationClip {
    std::string model;
    int modelAnimation = -1;
    float duration = 1;
    std::vector<AnimationTrack> tracks;
    std::vector<AnimationScriptEvent> events;
    std::shared_ptr<const Json> skeleton; // Runtime-only, geometry-free fade metadata.
};
inline bool animationAsset(const std::string& path) {
    return materialPathValid(path) && AssetManager::extension(path) == ".leanim";
}
inline Entity animationTarget(World& world, Entity owner, const std::string& path) {
    if (path.empty()) {
        return world.alive(owner) ? owner : Entity{};
    }
    std::size_t start = 0;
    Entity current = owner;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto name = path.substr(start, end == std::string::npos ? end : end - start);
        Entity found{};
        world.each<Parent, ObjectName>(
            [&](Entity child, const Parent& parent, const ObjectName& label) {
                if (found == Entity{} && parent.entity == current && label.value == name) {
                    found = child;
                }
            });
        if (found == Entity{}) {
            return {};
        }
        current = found;
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return current;
}
inline AnimationClip decodeAnimationClip(const Json& doc) {
    if (doc.at("format") != "LegacyEngineAnimation" || doc.at("version") != 1) {
        throw std::runtime_error("Unsupported animation clip");
    }
    AnimationClip clip;
    clip.duration = doc.at("duration").get<float>();
    clip.model = doc.value("model", std::string{});
    clip.modelAnimation = doc.value("modelAnimation", -1);
    if (!clip.model.empty() && (!materialPathValid(clip.model) || !modelAsset(clip.model) ||
                                clip.modelAnimation < 0 || clip.modelAnimation >= 256)) {
        throw std::runtime_error("Invalid model animation reference");
    }
    if (!std::isfinite(clip.duration) || clip.duration <= 0 || clip.duration > 86400) {
        throw std::runtime_error("Animation duration must be 0..86400 seconds");
    }
    const auto& tracks = doc.at("tracks");
    if (!tracks.is_array() || tracks.size() > 64) {
        throw std::runtime_error("Animation supports at most 64 tracks");
    }
    std::size_t total = 0;
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& data : tracks) {
        AnimationTrack track;
        track.target = data.value("target", std::string{});
        track.property = data.at("property");
        const auto* property = animationProperty(track.property);
        if (!property || track.target.size() > 1024 ||
            track.target.find('\0') != std::string::npos ||
            (!track.target.empty() && !materialPathValid(track.target)) ||
            !seen.emplace(track.target, track.property).second) {
            throw std::runtime_error("Invalid or duplicate animation track");
        }
        const auto& keys = data.at("keys");
        if (!keys.is_array() || keys.size() > 4096 || (total += keys.size()) > 16384) {
            throw std::runtime_error("Animation key limit exceeded");
        }
        float previous = -1;
        for (const auto& item : keys) {
            AnimationKey key;
            key.time = item.at("time");
            const auto& value = item.at("value");
            if (!std::isfinite(key.time) || key.time < 0 || key.time > clip.duration ||
                key.time <= previous || !value.is_array() ||
                value.size() != std::size_t(property->lanes())) {
                throw std::runtime_error("Invalid animation key");
            }
            for (int i = 0; i < property->lanes(); ++i) {
                key.value[i] = value.at(i).get<float>();
                if (!std::isfinite(key.value[i]) || key.value[i] < property->minimum ||
                    key.value[i] > property->maximum) {
                    throw std::runtime_error("Animation value out of range");
                }
            }
            previous = key.time;
            track.keys.push_back(key);
        }
        clip.tracks.push_back(std::move(track));
    }
    if (doc.contains("events")) {
        const auto& events = doc.at("events");
        if (!events.is_array() || events.size() > 256) {
            throw std::runtime_error("Animation event limit exceeded");
        }
        for (const auto& event : events) {
            AnimationScriptEvent call;
            call.time = event.at("time");
            call.target = event.value("target", std::string{});
            call.script = event.at("script");
            call.function = event.at("function");
            if (!std::isfinite(call.time) || call.time < 0 || call.time > clip.duration ||
                call.target.size() > 1024 ||
                (!call.target.empty() && !materialPathValid(call.target)) || call.script.empty() ||
                call.script.size() > 128 ||
                call.script.find_first_of("/\\#") != std::string::npos ||
                !animationFunctionName(call.function)) {
                throw std::runtime_error("Invalid animation script event");
            }
            clip.events.push_back(std::move(call));
        }
        std::stable_sort(clip.events.begin(), clip.events.end(), [](const auto& a, const auto& b) {
            return a.time < b.time;
        });
    }
    return clip;
}
inline Json encodeAnimationClip(const AnimationClip& clip) {
    Json tracks = Json::array();
    for (const auto& track : clip.tracks) {
        Json keys = Json::array();
        const auto* property = animationProperty(track.property);
        if (!property) {
            throw std::runtime_error("Unknown animation property");
        }
        for (const auto& key : track.keys) {
            Json value = Json::array();
            for (int i = 0; i < property->lanes(); ++i) {
                value.push_back(key.value[i]);
            }
            keys.push_back(Json{{"time", key.time}, {"value", value}});
        }
        tracks.push_back(
            Json{{"target", track.target}, {"property", track.property}, {"keys", keys}});
    }
    Json result{{"format", "LegacyEngineAnimation"},
                {"version", 1},
                {"duration", clip.duration},
                {"tracks", tracks}};
    result["events"] = Json::array();
    for (const auto& event : clip.events) {
        result["events"].push_back({{"time", event.time},
                                    {"target", event.target},
                                    {"script", event.script},
                                    {"function", event.function}});
    }
    if (!clip.model.empty()) {
        result["model"] = clip.model;
        result["modelAnimation"] = clip.modelAnimation;
    }
    decodeAnimationClip(result);
    return result;
}
// Share only skeleton/key data across clips for the same packed model. Never
// retain/copy its high-poly geometry just to interrupt a crossfade.
inline std::shared_ptr<const Json> animationSkeletonMetadata(const std::filesystem::path& file,
                                                             const Json& doc) {
    struct Entry {
        std::filesystem::file_time_type stamp;
        std::uintmax_t size;
        std::weak_ptr<const Json> data;
    };
    static std::mutex mutex;
    static std::map<std::filesystem::path, Entry> cache;
    const auto stamp = std::filesystem::last_write_time(file);
    const auto size = std::filesystem::file_size(file);
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto found = cache.find(file);
        if (found != cache.end() && found->second.stamp == stamp && found->second.size == size) {
            if (auto data = found->second.data.lock()) {
                return data;
            }
        }
    }
    auto result = std::make_shared<const Json>(
        Json{{"nodes", doc.at("nodes")}, {"animations", doc.at("animations")}});
    std::lock_guard<std::mutex> guard(mutex);
    auto found = cache.find(file);
    if (found != cache.end() && found->second.stamp == stamp && found->second.size == size) {
        if (auto data = found->second.data.lock()) {
            return data;
        }
    }
    if (cache.size() >= 32) {
        cache.erase(cache.begin());
    }
    cache.insert_or_assign(file, Entry{stamp, size, result});
    return result;
}
inline AnimationClip loadAnimationClip(const AssetManager& assets, const std::string& path) {
    if (!animationAsset(path)) {
        throw std::runtime_error("Expected .leanim clip");
    }
    const auto file = assets.resolve(path);
    std::shared_ptr<const Json> container;
    AnimationClip clip;
    if (embeddedAsset(path)) {
        container = readModelContainer(assetContainerPath(file));
        clip = decodeAnimationClip(container->at("embeddedAssets").at(embeddedAssetName(path)));
    } else {
        clip = decodeAnimationClip(readJsonFile(file, 4 * 1024 * 1024));
    }
    if (!clip.model.empty()) {
        const auto modelFile = assetContainerPath(assets.resolve(clip.model));
        if (!container || modelFile != assetContainerPath(file)) {
            container = readModelContainer(modelFile);
        }
        const auto& doc = *container;
        clip.skeleton = animationSkeletonMetadata(modelFile, doc);
        const auto& clips = doc.at("animations");
        if (!clips.is_array() || std::size_t(clip.modelAnimation) >= clips.size()) {
            throw std::runtime_error("Missing imported model animation");
        }
        clip.duration = clips[clip.modelAnimation].at("duration").get<float>();
        if (!std::isfinite(clip.duration) || clip.duration <= 0 || clip.duration > 86400) {
            throw std::runtime_error("Invalid model animation duration");
        }
    }
    return clip;
}
inline void
saveAnimationClip(const AssetManager& assets, const std::string& path, const AnimationClip& clip) {
    if (!animationAsset(path)) {
        throw std::runtime_error("Expected .leanim clip");
    }
    writeJsonFile(assets.resolve(path), encodeAnimationClip(clip));
}
inline AnimationValue sampleAnimationTrack(const AnimationTrack& track, float time) {
    if (track.keys.empty()) {
        return {};
    }
    auto upper = std::upper_bound(
        track.keys.begin(), track.keys.end(), time, [](float time, const AnimationKey& key) {
            return time < key.time;
        });
    if (upper == track.keys.begin()) {
        return upper->value;
    }
    if (upper == track.keys.end()) {
        return track.keys.back().value;
    }
    const auto& a = *(upper - 1);
    const auto& b = *upper;
    const auto* property = animationProperty(track.property);
    if (property->kind == AnimationValueKind::Boolean) {
        return a.value;
    }
    const float t = (time - a.time) / (b.time - a.time);
    if (property->kind == AnimationValueKind::Quaternion) {
        const auto q = nlerp(Quat{a.value[0], a.value[1], a.value[2], a.value[3]},
                             Quat{b.value[0], b.value[1], b.value[2], b.value[3]},
                             t);
        return {q.x, q.y, q.z, q.w};
    }
    AnimationValue value{};
    for (int i = 0; i < 4; ++i) {
        value[i] = a.value[i] + (b.value[i] - a.value[i]) * t;
    }
    return value;
}
inline void applyAnimationClip(World& world, Entity owner, const AnimationClip& clip, float time) {
    if (auto* a = world.tryGet<Animation>(owner)) {
        a->time = time;
        a->model = clip.model;
        a->modelAnimation = clip.modelAnimation;
        a->poseActive = !clip.model.empty();
    }
    for (const auto& track : clip.tracks) {
        if (!track.keys.empty()) {
            const auto* property = animationProperty(track.property);
            const auto target = animationTarget(world, owner, track.target);
            writeAnimationValue(world, target, *property, sampleAnimationTrack(track, time));
        }
    }
    updateHierarchy(world);
}
