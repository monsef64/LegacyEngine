#pragma once
#include "core/hierarchy.hpp"
#include "project/project_json.hpp"
#include "scene/camera.hpp"
#include "scene/scene_camera.hpp"
#include "scripting/script_exports.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

// Versioned, portable text format. Meshes use catalog indices, never GPU IDs.
namespace ProjectIO {
using Meshes = std::array<MeshHandle, 6>; // Grid, Plane, Cube, Sphere, Capsule, Cone
constexpr std::size_t maxBytes = 16 * 1024 * 1024, maxObjects = 10000, maxScript = 64 * 1024;
inline void require(bool ok) {
    if (!ok) {
        throw std::runtime_error("Invalid or unsupported project file");
    }
}
inline float number(std::istream& in) {
    float v = 0;
    in >> v;
    require(bool(in) && std::isfinite(v) && std::abs(v) <= 1e6f);
    return v;
}
inline void writeVec(std::ostream& out, Vec3 v) {
    out << v.x << ' ' << v.y << ' ' << v.z << ' ';
}
inline Vec3 readVec(std::istream& in) {
    const float x = number(in), y = number(in), z = number(in);
    return {x, y, z};
}
inline void writeTransform(std::ostream& out, const Transform& t) {
    writeVec(out, t.position);
    out << t.rotation.x << ' ' << t.rotation.y << ' ' << t.rotation.z << ' ' << t.rotation.w << ' ';
    writeVec(out, t.scale);
}
inline Transform readTransform(std::istream& in) {
    Transform t;
    t.position = readVec(in);
    t.rotation.x = number(in);
    t.rotation.y = number(in);
    t.rotation.z = number(in);
    t.rotation.w = number(in);
    t.scale = readVec(in);
    return t;
}
inline std::string encodeLegacy(World& world,
                                std::string_view source,
                                const OrbitCamera& camera,
                                const Meshes& meshes,
                                std::string_view assetDirectory = {}) {
    require(source.size() <= maxScript);
    const auto entities = world.entities();
    require(entities.size() <= maxObjects);
    auto entityIndex = [&](Entity target) {
        int index = -1;
        for (std::size_t i = 0; i < entities.size(); ++i) {
            if (entities[i] == target) {
                index = static_cast<int>(i);
            }
        }
        return index;
    };
    std::ostringstream out;
    out << std::setprecision(9) << "LegacyEngineProject 12\n";
    out << std::quoted(std::string(source)) << '\n'
        << std::quoted(std::string(assetDirectory)) << '\n';
    writeVec(out, camera.target);
    out << camera.yaw << ' ' << camera.pitch << ' ' << camera.distance << ' ' << camera.fieldOfView
        << '\n';
    out << entities.size() << '\n';
    auto writeFields = [&](const std::vector<ScriptField>& fields) {
        require(fields.size() <= 64);
        out << fields.size() << ' ';
        for (const auto& field : fields) {
            require(exportName(field.name) && exportType(field.type) && field.text.size() <= 4096 &&
                    std::isfinite(field.number) && std::abs(field.number) <= 1e6f);
            if (exportAssetType(field.type)) {
                require(exportAssetPath(field.text));
            }
            out << std::quoted(field.name) << ' ' << std::quoted(field.type) << ' '
                << std::quoted(field.text) << ' ' << field.number << ' ' << field.boolean << ' '
                << entityIndex(field.entity) << ' ';
        }
    };
    for (Entity e : entities) {
        unsigned mask = (world.has<ObjectName>(e) ? 1 : 0) | (world.has<Transform>(e) ? 2 : 0) |
                        (world.has<MeshRenderer>(e) ? 4 : 0) | (world.has<Collider>(e) ? 8 : 0) |
                        (world.has<RigidBody>(e) ? 16 : 0) | (world.has<LuaScript>(e) ? 32 : 0) |
                        (world.has<Camera>(e) ? 64 : 0) | (world.has<AudioSource>(e) ? 128 : 0) |
                        (world.has<AudioListener>(e) ? 256 : 0) |
                        (world.has<DirectionalLight>(e) ? 512 : 0) |
                        (world.has<PointLight>(e) ? 1024 : 0) |
                        (world.has<SpotLight>(e) ? 2048 : 0) | (world.has<Skybox>(e) ? 4096 : 0);
        int parent = -1;
        const Entity p = getParent(world, e);
        for (std::size_t i = 0; i < entities.size(); ++i) {
            if (entities[i] == p) {
                parent = static_cast<int>(i);
            }
        }
        out << mask << ' ' << parent << ' ';
        if (auto* n = world.tryGet<ObjectName>(e)) {
            out << std::quoted(n->value) << ' ';
        }
        if (auto* t = world.tryGet<Transform>(e)) {
            writeTransform(out, *t);
        }
        if (auto* m = world.tryGet<MeshRenderer>(e)) {
            int mesh = -1;
            for (int i = 0; i < 6; ++i) {
                if (meshes[i].id == m->mesh.id) {
                    mesh = i;
                }
            }
            require(mesh >= 0);
            out << mesh << ' ';
            writeVec(out, m->color);
            out << m->checkerboard << ' ' << m->specular << ' ';
            require(m->material.empty() || materialAsset(m->material));
            require(m->materialOverrides <= 127 && validMaterial(m->materialValues));
            out << std::quoted(m->material) << ' ' << m->materialOverrides << ' '
                << std::quoted(materialJson(m->materialValues).dump()) << ' ';
        }
        if (auto* c = world.tryGet<Collider>(e)) {
            writeVec(out, c->size);
            writeVec(out, c->center);
            out << c->friction << ' ' << c->bounciness << ' ' << static_cast<int>(c->shape) << ' '
                << c->radius << ' ' << c->height << ' ';
        }
        if (auto* b = world.tryGet<RigidBody>(e)) {
            out << b->mass << ' ' << b->useGravity << ' ';
            writeVec(out, b->linearVelocity);
            writeVec(out, b->angularVelocity);
        }
        if (auto* script = world.tryGet<LuaScript>(e)) {
            require(script->additional.size() <= 63);
            out << std::quoted(script->path) << ' ' << script->enabled << ' '
                << script->additional.size() << ' ';
            for (const auto& entry : script->additional) {
                out << std::quoted(entry.path) << ' ' << entry.enabled << ' ';
            }
            writeFields(script->fields);
            for (const auto& entry : script->additional) {
                writeFields(entry.fields);
            }
        }
        if (auto* c = world.tryGet<Camera>(e)) {
            require(validCamera(*c));
            int target = -1;
            for (std::size_t i = 0; i < entities.size(); ++i) {
                if (entities[i] == c->targetEntity) {
                    target = static_cast<int>(i);
                }
            }
            out << c->active << ' ' << c->orthographic << ' ' << c->fieldOfView << ' '
                << c->nearClip << ' ' << c->farClip << ' ' << c->orthographicSize << ' '
                << c->viewportX << ' ' << c->viewportY << ' ' << c->viewportWidth << ' '
                << c->viewportHeight << ' ' << target << ' ';
            writeVec(out, c->target);
            writeVec(out, c->followOffset);
            out << c->tracking << ' ' << c->following << ' ';
        }
        if (auto* a = world.tryGet<AudioSource>(e)) {
            out << std::quoted(a->clip) << ' ' << a->volume << ' ' << a->pitch << ' ' << a->pan
                << ' ' << a->distance << ' ' << a->looping << ' ' << a->muted << ' '
                << a->playOnStart << ' ' << a->blend << ' ';
        }
        if (auto* listener = world.tryGet<AudioListener>(e)) {
            out << listener->enabled << ' ';
        }
        auto writeLight = [&](const LightSettings& l) {
            require(validLight(l) && world.has<Transform>(e));
            writeVec(out, l.color);
            out << l.intensity << ' ' << l.enabled << ' ' << l.shadows << ' ' << l.range << ' '
                << l.innerAngle << ' ' << l.outerAngle << ' ' << l.shadowDistance << ' '
                << l.shadowBias << ' ' << l.shadowQuality << ' ';
        };
        if (auto* l = world.tryGet<DirectionalLight>(e)) {
            writeLight(*l);
        }
        if (auto* l = world.tryGet<PointLight>(e)) {
            writeLight(*l);
        }
        if (auto* l = world.tryGet<SpotLight>(e)) {
            writeLight(*l);
        }
        if (auto* sky = world.tryGet<Skybox>(e)) {
            require(validSkybox(*sky));
            out << sky->type << ' ';
            writeVec(out, sky->color);
            out << sky->enabled << ' ';
            for (const auto* path : {&sky->right,
                                     &sky->left,
                                     &sky->top,
                                     &sky->bottom,
                                     &sky->front,
                                     &sky->back,
                                     &sky->hdr}) {
                out << std::quoted(*path) << ' ';
            }
        }
        out << '\n';
    }
    auto text = out.str();
    require(text.size() <= maxBytes);
    return text;
}
struct Loaded {
    World world;
    std::string source, assetDirectory;
    OrbitCamera camera;
};
inline void decodeLegacy(const std::string& text, Loaded& result, const Meshes& meshes) {
    require(text.size() <= maxBytes);
    std::istringstream in(text);
    std::string magic;
    int version = 0;
    in >> magic >> version;
    require(magic == "LegacyEngineProject" && (version >= 1 && version <= 12));
    in >> std::quoted(result.source);
    require(result.source.size() <= maxScript && result.source.find('\0') == std::string::npos);
    if (version >= 4) {
        in >> std::quoted(result.assetDirectory);
        require(result.assetDirectory.size() <= 4096 &&
                result.assetDirectory.find('\0') == std::string::npos &&
                !std::filesystem::path(result.assetDirectory).is_absolute());
        for (const auto& part : std::filesystem::path(result.assetDirectory)) {
            require(part != "..");
        }
    }
    auto& camera = result.camera;
    camera.target = readVec(in);
    camera.yaw = number(in);
    camera.pitch = number(in);
    camera.distance = number(in);
    camera.fieldOfView = number(in);
    require(camera.distance > 0 && camera.fieldOfView > 0 && camera.fieldOfView < 3.14f);
    int count = -1;
    in >> count;
    require(bool(in) && count >= 0 && count <= static_cast<int>(maxObjects));
    std::vector<Entity> entities;
    std::vector<int> parents, cameraTargets;
    auto readFields = [&](std::vector<ScriptField>& fields) {
        int size = -1;
        in >> size;
        require(size >= 0 && size <= 64);
        for (int i = 0; i < size; ++i) {
            ScriptField field;
            int target = -1;
            in >> std::quoted(field.name) >> std::quoted(field.type) >> std::quoted(field.text);
            field.number = number(in);
            in >> field.boolean >> target;
            require(exportName(field.name) && exportType(field.type) && field.text.size() <= 4096 &&
                    field.text.find('\0') == std::string::npos && target >= -1 && target < count);
            if (exportAssetType(field.type)) {
                require(!std::filesystem::path(field.text).is_absolute());
                for (const auto& part : std::filesystem::path(field.text)) {
                    require(part != "..");
                }
            }
            if (target >= 0) {
                field.entity = {static_cast<std::uint32_t>(target), 0};
            }
            fields.push_back(field);
        }
    };
    for (int i = 0; i < count; ++i) {
        unsigned mask = 0;
        int parent = -1;
        in >> mask >> parent;
        require(bool(in) &&
                mask <= (version == 1   ? 31u
                         : version == 2 ? 63u
                         : version < 6  ? 127u
                         : version < 7  ? 255u
                         : version < 9  ? 511u
                         : version < 12 ? 4095u
                                        : 8191u) &&
                parent >= -1 && parent < count && parent != i);
        const Entity e = result.world.create();
        entities.push_back(e);
        parents.push_back(parent);
        cameraTargets.push_back(-1);
        if (mask & 1) {
            std::string name;
            in >> std::quoted(name);
            require(name.size() <= 4096);
            result.world.add(e, ObjectName{name});
        }
        if (mask & 2) {
            result.world.add(e, readTransform(in));
        }
        if (mask & 4) {
            int mesh = -1;
            in >> mesh;
            require(mesh >= 0 && mesh < 6);
            MeshRenderer m;
            m.mesh = meshes[mesh];
            m.color = readVec(in);
            in >> m.checkerboard;
            m.specular = number(in);
            if (version >= 11) {
                std::string values;
                in >> std::quoted(m.material) >> m.materialOverrides >> std::quoted(values);
                require(values.size() <= 64 * 1024 && m.materialOverrides <= 127 &&
                        (m.material.empty() || materialAsset(m.material)));
                m.materialValues = parseMaterial(parseJson(values));
            }
            result.world.add(e, m);
        }
        if (mask & 8) {
            Collider c;
            c.size = readVec(in);
            c.center = readVec(in);
            c.friction = number(in);
            c.bounciness = number(in);
            int shape = -1;
            in >> shape;
            require(shape >= 0 && shape < 4);
            c.shape = static_cast<ColliderShape>(shape);
            c.radius = number(in);
            c.height = number(in);
            require(c.radius > 0 && c.height >= 0);
            result.world.add(e, c);
        }
        if (mask & 16) {
            RigidBody b;
            b.mass = number(in);
            in >> b.useGravity;
            b.linearVelocity = readVec(in);
            b.angularVelocity = readVec(in);
            require(b.mass >= 0);
            result.world.add(e, b);
        }
        if (mask & 32) {
            LuaScript script;
            in >> std::quoted(script.path) >> script.enabled;
            require(script.path.size() <= 4096 && script.path.find('\0') == std::string::npos &&
                    !std::filesystem::path(script.path).is_absolute());
            for (const auto& part : std::filesystem::path(script.path)) {
                require(part != "..");
            }
            if (version >= 5) {
                int extras = 0;
                in >> extras;
                require(extras >= 0 && extras <= 63);
                for (int j = 0; j < extras; ++j) {
                    ScriptAttachment entry;
                    in >> std::quoted(entry.path) >> entry.enabled;
                    require(entry.path.size() <= 4096 &&
                            entry.path.find('\0') == std::string::npos &&
                            !std::filesystem::path(entry.path).is_absolute());
                    for (const auto& part : std::filesystem::path(entry.path)) {
                        require(part != "..");
                    }
                    script.additional.push_back(entry);
                }
            }
            if (version >= 8) {
                readFields(script.fields);
                for (auto& entry : script.additional) {
                    readFields(entry.fields);
                }
            }
            result.world.add(e, script);
        }
        if (mask & 64) {
            require((mask & 2) != 0);
            Camera c;
            in >> c.active >> c.orthographic;
            c.fieldOfView = number(in);
            c.nearClip = number(in);
            c.farClip = number(in);
            c.orthographicSize = number(in);
            c.viewportX = number(in);
            c.viewportY = number(in);
            c.viewportWidth = number(in);
            c.viewportHeight = number(in);
            in >> cameraTargets.back();
            require(cameraTargets.back() >= -1 && cameraTargets.back() < count &&
                    cameraTargets.back() != i);
            c.target = readVec(in);
            c.followOffset = readVec(in);
            in >> c.tracking >> c.following;
            require(validCamera(c));
            result.world.add(e, c);
        }
        if (mask & 128) {
            AudioSource a;
            in >> std::quoted(a.clip);
            require(a.clip.size() <= 4096 && a.clip.find('\0') == std::string::npos &&
                    !std::filesystem::path(a.clip).is_absolute());
            for (const auto& part : std::filesystem::path(a.clip)) {
                require(part != "..");
            }
            a.volume = number(in);
            a.pitch = number(in);
            a.pan = number(in);
            a.distance = number(in);
            in >> a.looping >> a.muted >> a.playOnStart;
            if (version >= 7) {
                a.blend = number(in);
            }
            require(a.blend >= 0 && a.blend <= 1);
            require(a.volume >= 0 && a.volume <= 1 && a.pitch >= .1f && a.pitch <= 4 &&
                    a.pan >= -1 && a.pan <= 1 && a.distance >= 0);
            result.world.add(e, a);
        }
        if (mask & 256) {
            AudioListener listener;
            in >> listener.enabled;
            result.world.add(e, listener);
        }
        auto readLight = [&](auto light) {
            light.color = readVec(in);
            light.intensity = number(in);
            in >> light.enabled >> light.shadows;
            light.range = number(in);
            light.innerAngle = number(in);
            light.outerAngle = number(in);
            light.shadowDistance = number(in);
            light.shadowBias = number(in);
            if (version >= 10) {
                in >> light.shadowQuality;
            }
            require(validLight(light) && (mask & 2));
            result.world.add(e, light);
        };
        if (mask & 512) {
            readLight(DirectionalLight{});
        }
        if (mask & 1024) {
            readLight(PointLight{});
        }
        if (mask & 2048) {
            readLight(SpotLight{});
        }
        if (mask & 4096) {
            Skybox sky;
            in >> sky.type;
            sky.color = readVec(in);
            in >> sky.enabled;
            for (auto* path :
                 {&sky.right, &sky.left, &sky.top, &sky.bottom, &sky.front, &sky.back, &sky.hdr}) {
                in >> std::quoted(*path);
            }
            require(validSkybox(sky));
            result.world.add(e, sky);
        }
        require(bool(in));
    }
    result.world.each<LuaScript>([&](Entity, LuaScript& scripts) {
        auto remap = [&](std::vector<ScriptField>& fields) {
            for (auto& field : fields) {
                if (field.entity != Entity{}) {
                    field.entity = entities[field.entity.index];
                }
            }
        };
        remap(scripts.fields);
        for (auto& entry : scripts.additional) {
            remap(entry.fields);
        }
    });
    for (int i = 0; i < count; ++i) {
        if (cameraTargets[i] >= 0) {
            require(result.world.has<Transform>(entities[cameraTargets[i]]));
            result.world.get<Camera>(entities[i]).targetEntity = entities[cameraTargets[i]];
        }
    }
    int activeCount = 0;
    result.world.each<Camera>([&](Entity, Camera& c) {
        if (c.active) {
            ++activeCount;
        }
    });
    require(activeCount <= 1);
    for (int i = 0; i < count; ++i) {
        if (parents[i] >= 0) {
            require(setParent(result.world, entities[i], entities[parents[i]]));
        }
    }
    in >> std::ws;
    require(in.eof());
}
inline std::string encode(World& world,
                          std::string_view source,
                          const OrbitCamera& camera,
                          const Meshes& meshes,
                          std::string_view assetDirectory = {}) {
    const auto document = ProjectJson::fromLegacy(encodeLegacy(
        world, source, camera, meshes, assetDirectory.empty() ? "Assets" : assetDirectory));
    auto text = document.dump(2);
    require(text.size() <= maxBytes);
    return text;
}
inline void decode(const std::string& text, Loaded& result, const Meshes& meshes) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first != text.npos && text[first] == '{') {
        require(text.size() <= maxBytes);
        decodeLegacy(ProjectJson::toLegacy(parseJson(text)), result, meshes);
    } else {
        decodeLegacy(text, result, meshes);
    }
}
inline bool save(const std::string& path,
                 World& world,
                 std::string_view source,
                 const OrbitCamera& camera,
                 const Meshes& meshes,
                 std::string& error) {
    std::filesystem::path temporary;
    try {
        auto text = encode(world, source, camera, meshes, "Assets");
        Loaded check;
        decode(text, check, meshes); // Never replace a project with invalid values.
        auto document = parseJson(text);
        const auto settingsPath =
            std::filesystem::absolute(path).parent_path() / "Settings" / "ProjectSettings.json";
        writeJsonFile(settingsPath,
                      Json{{"version", 1}, {"editorCamera", document.at("editorCamera")}});
        document.erase("editorCamera");
        document["projectSettings"] = "Settings/ProjectSettings.json";
        text = document.dump(2);
        temporary = path + ".tmp-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Could not create project file");
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            throw std::runtime_error("Could not write project file");
        }
        std::filesystem::rename(temporary, path);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        if (!temporary.empty()) {
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
        }
        return false;
    }
}
inline bool load(const std::string& path,
                 World& world,
                 std::string& source,
                 OrbitCamera& camera,
                 const Meshes& meshes,
                 std::string& error,
                 std::string* assetDirectory = nullptr) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Could not open project file");
        }
        in.seekg(0, std::ios::end);
        auto size = in.tellg();
        require(size >= 0 && size <= static_cast<std::streamoff>(maxBytes));
        in.seekg(0);
        std::string text(static_cast<std::size_t>(size), '\0');
        in.read(text.data(), size);
        require(bool(in));
        if (text.find_first_not_of(" \t\r\n") != text.npos &&
            text[text.find_first_not_of(" \t\r\n")] == '{') {
            auto document = parseJson(text);
            if (document.contains("projectSettings")) {
                require(document.at("projectSettings") == "Settings/ProjectSettings.json");
                const auto settingsPath = std::filesystem::absolute(path).parent_path() /
                                          "Settings" / "ProjectSettings.json";
                const auto settings = readJsonFile(settingsPath, 64 * 1024);
                require(settings.at("version") == 1);
                document["editorCamera"] = settings.at("editorCamera");
            }
            text = document.dump();
        }
        Loaded loaded;
        decode(text, loaded, meshes);
        world.restore(loaded.world);
        source = std::move(loaded.source);
        camera = loaded.camera;
        if (assetDirectory) {
            *assetDirectory = std::move(loaded.assetDirectory);
        }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
} // namespace ProjectIO
