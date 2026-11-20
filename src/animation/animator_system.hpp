#pragma once
#include "animation/animator_sheet.hpp"

class AnimatorSystem {
    struct Cached {
        Json document;
        std::filesystem::file_time_type stamp{};
        double checked = -1;
        unsigned revision = 0;
    };
    std::map<std::string, Cached> sheets_;
    std::filesystem::path root_;
    float clock_ = 0;

  public:
    void clear() {
        sheets_.clear();
        root_.clear();
        clock_ = 0;
    }
    void update(World& world, const AssetManager& assets, float seconds) {
        if (root_ != assets.root) {
            clear();
            root_ = assets.root;
        }
        clock_ += seconds;
        world.each<Animator>([&](Entity e, Animator& a) {
            if (!isActive(world, e)) {
                return;
            }
            if (!a.enabled) {
                if (a.initialized) {
                    if (auto* animation = world.tryGet<Animation>(e)) {
                        animation->request(AnimationAction::Stop);
                    }
                    a.initialized = false;
                }
                return;
            }
            try {
                if (!Animators::asset(a.sheet)) {
                    throw std::runtime_error("Assign an AnimatorSheet asset");
                }
                if (sheets_.size() > 128 && !sheets_.count(a.sheet)) {
                    throw std::runtime_error("Animator sheet cache limit reached");
                }
                auto& cached = sheets_[a.sheet];
                if (cached.checked < 0 || clock_ - cached.checked > .5f) {
                    cached.checked = clock_;
                    const auto stamp = std::filesystem::last_write_time(assets.resolve(a.sheet));
                    if (cached.document.is_null() || stamp != cached.stamp) {
                        auto next = readJsonFile(assets.resolve(a.sheet), 2 * 1024 * 1024);
                        Animators::validate(next);
                        cached.document = std::move(next);
                        cached.stamp = stamp;
                        ++cached.revision;
                    }
                }
                const auto& doc = cached.document;
                if (a.loadedSheet != a.sheet || a.sheetRevision != cached.revision) {
                    if (!a.loadedSheet.empty()) {
                        a.parameters.clear();
                        a.parameterTypes.clear();
                    }
                    a.initialized = false;
                    a.currentState.clear();
                    a.loadedSheet = a.sheet;
                    a.sheetRevision = cached.revision;
                }
                std::set<std::string> known;
                for (const auto& p : doc.at("parameters")) {
                    const auto name = p.at("name").get<std::string>();
                    known.insert(name);
                    a.parameterTypes[name] = p.at("type").get<std::string>();
                    if (!a.parameters.count(name)) {
                        a.parameters[name] = p.at("default");
                    }
                    if (p.at("type") == "float" ? !a.parameters[name].is_number()
                                                : !a.parameters[name].is_boolean()) {
                        throw std::runtime_error("Animator parameter type mismatch: " + name);
                    }
                }
                for (const auto& p : a.parameters) {
                    if (!known.count(p.first)) {
                        throw std::runtime_error("Unknown animator parameter: " + p.first);
                    }
                }
                auto& animation =
                    world.has<Animation>(e) ? world.get<Animation>(e) : world.add(e, Animation{});
                auto enter = [&](const std::string& name, float fade) {
                    for (const auto& s : doc.at("states")) {
                        if (s.at("name") == name) {
                            const auto clip = s.at("clip").get<std::string>();
                            if (clip.empty()) {
                                throw std::runtime_error("Assign a clip to animator state: " +
                                                         name);
                            }
                            const auto next = loadAnimationClip(assets, clip);
                            if (a.initialized && !animation.model.empty() &&
                                next.model != animation.model) {
                                throw std::runtime_error(
                                    "Animator cross-fade clips must target the same model");
                            }
                            animation.loop = s.at("loop").get<bool>();
                            animation.playOnStart = false;
                            if (!a.initialized) {
                                animation.clip = clip;
                                if (!animation.request(AnimationAction::Play)) {
                                    throw std::runtime_error("Animation command queue full");
                                }
                            } else if (!animation.request(AnimationAction::CrossFade, clip, fade)) {
                                throw std::runtime_error("Animation command queue full");
                            }
                            a.currentState = name;
                            a.elapsed = 0;
                            a.stateDuration = next.duration;
                            a.initialized = true;
                            return;
                        }
                    }
                    throw std::runtime_error("Animator state not found: " + name);
                };
                if (!a.initialized) {
                    enter(a.pendingState.empty() ? doc.at("entry").get<std::string>()
                                                 : a.pendingState,
                          0);
                }
                if (!a.pendingState.empty()) {
                    const auto next = std::exchange(a.pendingState, {});
                    if (next != a.currentState) {
                        enter(next, a.pendingFade);
                    }
                } else {
                    a.elapsed += seconds;
                    for (const auto& t : doc.at("transitions")) {
                        if ((t.at("from") != "*" && t.at("from") != a.currentState) ||
                            t.at("to") == a.currentState) {
                            continue;
                        }
                        const float exit = t.value("exitTime", -1.f);
                        if (exit >= 0) {
                            if (a.elapsed < a.stateDuration * exit) {
                                continue;
                            }
                        }
                        bool pass = true;
                        std::vector<std::string> triggers;
                        for (const auto& c : t.at("conditions")) {
                            const auto p = c.at("parameter").get<std::string>(),
                                       op = c.at("op").get<std::string>();
                            const auto& value = a.parameters.at(p);
                            if (op == "trigger") {
                                pass &= value.get<bool>();
                                triggers.push_back(p);
                            } else if (op == "==") {
                                pass &= value == c.at("value");
                            } else if (op == "!=") {
                                pass &= value != c.at("value");
                            } else if (op == ">") {
                                pass &= value.get<double>() > c.at("value").get<double>();
                            } else {
                                pass &= value.get<double>() < c.at("value").get<double>();
                            }
                        }
                        if (pass) {
                            enter(t.at("to").get<std::string>(), t.value("duration", .2f));
                            for (const auto& p : triggers) {
                                a.parameters[p] = false;
                            }
                            break;
                        }
                    }
                }
                a.error.clear();
            } catch (const std::exception& error) {
                a.error = error.what();
                if (a.initialized) {
                    if (auto* animation = world.tryGet<Animation>(e)) {
                        animation->request(AnimationAction::Stop);
                    }
                    a.initialized = false;
                }
            }
        });
    }
};
