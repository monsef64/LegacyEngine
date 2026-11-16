#pragma once
#include "assets/assets.hpp"
#include "materials/material.hpp"
#include "materials/material_io.hpp"
#include "project/json_io.hpp"
#include "terrain/terrain.hpp"
#include <iostream>
#include <map>
namespace Terrains {
inline bool asset(const std::string& p) {
    return materialPathValid(p) && std::filesystem::path(p).extension() == ".terrain";
}
inline Json document(const Data& d) {
    Json layers = Json::array();
    for (const auto& l : d.layers) {
        layers.push_back({{"texture", l.texture},
                          {"color", {l.color.x, l.color.y, l.color.z}},
                          {"tiling", l.tiling},
                          {"normalMap", l.normalMap},
                          {"material", l.material},
                          {"normalStrength", l.normalStrength}});
    }
    return {{"format", "LegacyEngineTerrain"},
            {"version", 1},
            {"resolution", d.resolution},
            {"size", d.size},
            {"heights", d.heights},
            {"weights", d.weights},
            {"layers", layers},
            {"lightmap", d.lightmap}};
}
inline std::shared_ptr<Data> parse(const Json& j) {
    if (j.at("format") != "LegacyEngineTerrain" || j.at("version") != 1) {
        throw std::runtime_error("Unsupported terrain asset");
    }
    auto d = std::make_shared<Data>();
    d->lightmap = j.value("lightmap", std::string{});
    if (!materialTexturePath(d->lightmap)) {
        throw std::runtime_error("Invalid terrain lightmap");
    }
    d->resolution = j.at("resolution").get<int>();
    d->size = j.at("size").get<float>();
    if ((d->resolution != 65 && d->resolution != 129 && d->resolution != 257) ||
        !std::isfinite(d->size) || d->size < 1 || d->size > 10000) {
        throw std::runtime_error("Terrain requires 65/129/257 samples and size 1..10000m");
    }
    const auto count = std::size_t(d->resolution) * d->resolution;
    if (j.at("heights").size() != count || j.at("weights").size() != count ||
        j.at("layers").size() != 4) {
        throw std::runtime_error("Invalid terrain sample counts");
    }
    d->heights = j.at("heights").get<std::vector<float>>();
    d->weights = j.at("weights").get<std::vector<std::array<float, 4>>>();
    for (float h : d->heights) {
        if (!std::isfinite(h) || std::abs(h) > 10000) {
            throw std::runtime_error("Terrain heights exceed +/-10000m");
        }
    }
    for (auto& w : d->weights) {
        float total = 0;
        for (float v : w) {
            if (!std::isfinite(v) || v < 0 || v > 1) {
                throw std::runtime_error("Invalid terrain paint weight");
            }
            total += v;
        }
        if (total < .001f) {
            w = {1, 0, 0, 0};
        } else {
            for (float& v : w) {
                v /= total;
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        const auto& l = j.at("layers")[i];
        auto& out = d->layers[i];
        out.texture = l.at("texture").get<std::string>();
        out.normalMap = l.value("normalMap", std::string{});
        out.material = l.value("material", std::string{});
        if (!out.material.empty() && !materialAsset(out.material)) {
            throw std::runtime_error("Invalid terrain material");
        }
        out.normalStrength = l.value("normalStrength", 1.f);
        if (!materialTexturePath(out.normalMap) || !std::isfinite(out.normalStrength) ||
            out.normalStrength < 0 || out.normalStrength > 4) {
            throw std::runtime_error("Invalid terrain normal map");
        }
        const auto& c = l.at("color");
        if (!c.is_array() || c.size() != 3) {
            throw std::runtime_error("Invalid terrain color");
        }
        out.color = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
        out.tiling = l.at("tiling").get<float>();
        if (!materialTexturePath(out.texture) || !std::isfinite(out.tiling) || out.tiling < .01f ||
            out.tiling > 1000 || !std::isfinite(out.color.x) || !std::isfinite(out.color.y) ||
            !std::isfinite(out.color.z) || out.color.x < 0 || out.color.y < 0 || out.color.z < 0 ||
            out.color.x > 1 || out.color.y > 1 || out.color.z > 1) {
            throw std::runtime_error("Invalid terrain layer");
        }
    }
    d->chunks.assign(chunkCount(*d) * chunkCount(*d), 0);
    return d;
}
inline void load(Terrain& t, const AssetManager& assets) {
    if (t.asset.empty()) {
        return;
    }
    const auto path = assets.resolve(t.asset).generic_string();
    if (t.loaded == "error:" + t.asset) {
        return;
    }
    if (t.loaded != path) {
        if (!asset(t.asset)) {
            throw std::runtime_error("Expected .terrain asset");
        }
        t.data = parse(readJsonFile(path, 32 * 1024 * 1024));
        t.loaded = path;
        t.savedRevision = t.data->revision;
    }
}
inline void save(Terrain& t, const AssetManager& assets) {
    if (!t.data) {
        throw std::runtime_error("Terrain data unavailable");
    }
    if (t.asset.empty()) {
        std::filesystem::create_directories(assets.resolve("Terrains"));
        for (unsigned n = 1;; ++n) {
            t.asset = "Terrains/Terrain-" + std::to_string(n) + ".terrain";
            if (!std::filesystem::exists(assets.resolve(t.asset))) {
                break;
            }
        }
    }
    if (!asset(t.asset)) {
        throw std::runtime_error("Expected .terrain asset");
    }
    parse(document(*t.data));
    writeJsonFile(assets.resolve(t.asset), document(*t.data));
    t.loaded = assets.resolve(t.asset).generic_string();
    t.savedRevision = t.data->revision;
}
inline void saveAll(World& world, const AssetManager& assets) {
    std::map<std::string, unsigned> written;
    world.each<Terrain>([&](Entity, Terrain& t) {
        load(t, assets);
        if (!t.data) {
            throw std::runtime_error("Terrain data unavailable");
        }
        auto signature = t.data->revision;
        auto existing = written.find(t.asset);
        if (!t.asset.empty() && existing != written.end() && existing->second != signature) {
            t.asset.clear();
            t.loaded.clear();
        }
        if (t.asset.empty() || t.savedRevision != t.data->revision) {
            save(t, assets);
        }
        written[t.asset] = signature;
    });
}
inline void update(World& world, const AssetManager& assets) {
    world.each<Terrain>([&](Entity, Terrain& t) {
        try {
            load(t, assets);
        } catch (const std::exception& e) {
            if (t.loaded != "error:" + t.asset) {
                std::cerr << "Terrain: " << e.what() << "\n";
                t.loaded = "error:" + t.asset;
            }
            t.data.reset();
        }
    });
}
} // namespace Terrains
