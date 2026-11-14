#pragma once
#include "core/hierarchy.hpp"
#include <deque>
#include <functional>
#include <string>
#include <utility>

// Value-only signature: never compare struct padding or pointers into pools.
inline std::string sceneSignature(World& world) {
    std::string result;
    auto scalar = [&](auto value) {
        result.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    auto vec = [&](Vec3 v) {
        scalar(v.x);
        scalar(v.y);
        scalar(v.z);
    };
    auto transform = [&](const Transform& t) {
        vec(t.position);
        scalar(t.rotation.x);
        scalar(t.rotation.y);
        scalar(t.rotation.z);
        scalar(t.rotation.w);
        vec(t.scale);
    };
    auto component = [&](Entity e, auto value, auto write) {
        using T = decltype(value);
        const auto* c = world.tryGet<T>(e);
        scalar(c != nullptr);
        if (c) {
            write(*c);
        }
    };
    auto fields = [&](const std::vector<ScriptField>& entries) {
        scalar(entries.size());
        for (const auto& f : entries) {
            scalar(f.name.size());
            result += f.name;
            scalar(f.type.size());
            result += f.type;
            scalar(f.text.size());
            result += f.text;
            scalar(f.number);
            scalar(f.boolean);
            scalar(f.entity.index);
            scalar(f.entity.generation);
        }
    };
    for (Entity e : world.entities()) {
        component(e, ObjectActivity{}, [&](const ObjectActivity& a) { scalar(a.enabled); });
        component(e, PrefabInstance{}, [&](const PrefabInstance& p) {
            scalar(p.asset.size());
            result += p.asset;
        });
        scalar(e.index);
        scalar(e.generation);
        component(e, ObjectName{}, [&](const ObjectName& n) {
            scalar(n.value.size());
            result += n.value;
        });
        component(e, LuaScript{}, [&](const LuaScript& script) {
            scalar(script.path.size());
            result += script.path;
            scalar(script.enabled);
            fields(script.fields);
            scalar(script.additional.size());
            for (const auto& entry : script.additional) {
                scalar(entry.path.size());
                result += entry.path;
                scalar(entry.enabled);
                fields(entry.fields);
            }
        });
        auto light = [&](const LightSettings& l) {
            vec(l.color);
            scalar(l.intensity);
            scalar(l.enabled);
            scalar(l.shadows);
            scalar(l.range);
            scalar(l.innerAngle);
            scalar(l.outerAngle);
            scalar(l.shadowDistance);
            scalar(l.shadowBias);
            scalar(l.shadowQuality);
        };
        component(e, DirectionalLight{}, light);
        component(e, PointLight{}, light);
        component(e, SpotLight{}, light);
        component(e, CharacterController{}, [&](const CharacterController& c) {
            scalar(c.speed);
            scalar(c.sprintMultiplier);
            scalar(c.jumpSpeed);
            scalar(c.groundProbe);
            scalar(c.canMove);
            scalar(c.canJump);
            scalar(c.canSprint);
        });
        component(e, FPSCamera{}, [&](const FPSCamera& f) {
            scalar(f.isFreecam);
            scalar(f.mouseLook);
            scalar(f.speed);
            scalar(f.sensitivity);
        });
        component(e, Skybox{}, [&](const Skybox& sky) {
            scalar(sky.cubemap.size());
            result += sky.cubemap;
            scalar(sky.type);
            vec(sky.color);
            scalar(sky.enabled);
            for (const auto* p :
                 {&sky.right, &sky.left, &sky.top, &sky.bottom, &sky.front, &sky.back, &sky.hdr}) {
                scalar(p->size());
                result += *p;
            }
        });
        component(
            e, AudioListener{}, [&](const AudioListener& listener) { scalar(listener.enabled); });
        component(e, AudioSource{}, [&](const AudioSource& a) {
            scalar(a.clip.size());
            result += a.clip;
            scalar(a.volume);
            scalar(a.pitch);
            scalar(a.pan);
            scalar(a.distance);
            scalar(a.blend);
            scalar(a.looping);
            scalar(a.muted);
            scalar(a.playOnStart);
        });
        component(e, Camera{}, [&](const Camera& c) {
            scalar(c.active);
            scalar(c.orthographic);
            scalar(c.fieldOfView);
            scalar(c.nearClip);
            scalar(c.farClip);
            scalar(c.orthographicSize);
            scalar(c.viewportX);
            scalar(c.viewportY);
            scalar(c.viewportWidth);
            scalar(c.viewportHeight);
            scalar(c.targetEntity.index);
            scalar(c.targetEntity.generation);
            vec(c.target);
            vec(c.followOffset);
            scalar(c.tracking);
            scalar(c.following);
        });
        component(e, ObjectLayer{}, [&](const ObjectLayer& layer) { scalar(layer.value); });
        component(e, InspectorOrder{}, [&](const InspectorOrder& order) {
            scalar(order.types.size());
            for (int id : order.types) {
                scalar(id);
            }
        });
        component(e, Raycast{}, [&](const Raycast& q) {
            scalar(q.type);
            scalar(q.distance);
            scalar(q.radius);
            scalar(q.angle);
            scalar(q.samples);
            vec(q.offset);
            scalar(q.layerMask);
            scalar(q.includeTriggers);
        });
        component(e, Transform{}, transform);
        component(e, MeshRenderer{}, [&](const MeshRenderer& m) {
            if (m.model.empty()) {
                scalar(m.mesh.id);
            }
            scalar(m.model.size());
            result += m.model;
            vec(m.color);
            scalar(m.checkerboard);
            scalar(m.specular);
            scalar(m.material.size());
            result += m.material;
            scalar(m.materialOverrides);
            const auto& p = m.materialValues;
            scalar(p.shader);
            vec(p.color);
            scalar(p.texture.size());
            result += p.texture;
            scalar(p.normalMap.size());
            result += p.normalMap;
            scalar(p.normalStrength);
            scalar(p.tilingX);
            scalar(p.tilingY);
            scalar(p.specular);
        });
        component(e, Collider{}, [&](const Collider& c) {
            vec(c.size);
            vec(c.center);
            scalar(c.friction);
            scalar(c.bounciness);
            scalar(c.shape);
            scalar(c.radius);
            scalar(c.height);
            scalar(c.isTrigger);
        });
        component(e, RigidBody{}, [&](const RigidBody& b) {
            scalar(b.mass);
            scalar(b.useGravity);
            vec(b.linearVelocity);
            vec(b.angularVelocity);
            scalar(b.drag);
            scalar(b.angularDrag);
            scalar(b.isKinematic);
        });
        component(e, Parent{}, [&](const Parent& p) {
            scalar(p.entity.index);
            scalar(p.entity.generation);
            transform(p.previous);
        });
    }
    return result;
}

class EditorHistory {
  public:
    explicit EditorHistory(World& world, Entity selection = {}) {
        baseline_ = capture(world, selection);
    }
    // Call after scene UI, but only in Edit mode. Active gestures are committed
    // together when the pointer/text widget is released, not once per frame.
    void observe(World& world, Entity selection, bool gestureActive) {
        if (!gestureActive) {
            commit(world, selection);
        }
    }
    bool canUndo() const {
        return !undo_.empty();
    }
    bool canRedo() const {
        return !redo_.empty();
    }
    void commit(World& world, Entity selection) {
        auto signature = sceneSignature(world);
        if (signature == baseline_.signature) {
            baseline_.selection = selection;
            return;
        }
        push(undo_, std::move(baseline_));
        redo_.clear();
        baseline_ = capture(world, selection, std::move(signature));
    }
    std::string error;
    bool perform(World& world, Entity selection, std::function<void(bool)> action) {
        commit(world, selection);
        try {
            action(true);
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
        push(undo_, std::move(baseline_));
        redo_.clear();
        baseline_ = capture(world, selection);
        baseline_.action = std::move(action);
        error.clear();
        return true;
    }
    bool undo(World& world, Entity& selection) {
        return restore(world, selection, undo_, redo_);
    }
    bool redo(World& world, Entity& selection) {
        return restore(world, selection, redo_, undo_);
    }

  private:
    struct Entry {
        std::unique_ptr<World> scene;
        Entity selection;
        std::string signature;
        std::function<void(bool)> action;
    };
    Entry baseline_;
    std::deque<Entry> undo_, redo_;
    static constexpr std::size_t limit = 64;
    static Entry capture(World& world, Entity selection, std::string signature = {}) {
        if (signature.empty()) {
            signature = sceneSignature(world);
        }
        return {world.snapshot(), selection, std::move(signature), {}};
    }
    static void push(std::deque<Entry>& stack, Entry entry) {
        if (stack.size() == limit) {
            stack.pop_front();
        }
        stack.push_back(std::move(entry));
    }
    bool restore(World& world, Entity& selection, std::deque<Entry>& from, std::deque<Entry>& to) {
        if (from.empty()) {
            return false;
        }
        try {
            if (&from == &undo_) {
                if (baseline_.action) {
                    baseline_.action(false);
                }
            } else if (from.back().action) {
                from.back().action(true);
            }
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
        error.clear();
        push(to, std::move(baseline_));
        baseline_ = std::move(from.back());
        from.pop_back();
        world.restore(*baseline_.scene);
        selection = baseline_.selection;
        if (!world.alive(selection)) {
            selection = {};
        }
        return true;
    }
};
