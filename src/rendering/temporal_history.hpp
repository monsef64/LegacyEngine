// Per-view camera and object history for temporal rendering. This only tracks
// inputs; it does not perform upscaling or allocate GPU resources.
#pragma once

#include "core/math.hpp"
#include "rendering/mesh.hpp"
#include "rendering/temporal_sampling.hpp"
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

class TemporalHistory {
  public:
    struct Configuration {
        TemporalSampling::Extent render, display;
        // Callers change these when a different scene, camera, viewport, or
        // projection is used. Each view must have its own TemporalHistory.
        std::uint64_t scene = 0, camera = 0, projection = 0;
    };
    struct Frame {
        Mat4 currentViewProjection, previousViewProjection;
        TemporalSampling::Offset jitter;
        std::uint64_t index = 0;
        bool reset = true;
    };
    struct Object {
        Mat4 currentViewProjectionModel, previousViewProjectionModel;
        // False means newly visible/replaced geometry or a temporal reset.
        // A reactive mask or equivalent is still needed for such pixels.
        bool hasHistory = false;
    };

    struct DeformedObject {
        Object transform;
        // Valid only until the next commit/invalidate. Null on a reset or if
        // topology or asset identity changed since the preceding frame.
        const MeshData* previousMesh = nullptr;
    };

    // Begin is transactional: discard() leaves the last successfully rendered
    // frame untouched. forceReset is required for cuts, long visibility gaps,
    // or any other discontinuity not represented by Configuration.
    Frame begin(const Configuration& config, const Mat4& viewProjection, bool forceReset = false) {
        if (inFrame_) {
            throw std::logic_error("Temporal frame already in progress");
        }
        validate(config, viewProjection);
        const bool reset = forceReset || !valid_ || !same(config, committedConfig_);
        pendingConfig_ = config;
        pendingViewProjection_ = viewProjection;
        pendingIndex_ = reset ? 0 : committedIndex_ + 1;
        pendingReset_ = reset;
        pendingObjects_.clear();
        inFrame_ = true;
        const auto phases = TemporalSampling::phaseCount(config.render.width, config.display.width);
        return {viewProjection,
                reset ? viewProjection : committedViewProjection_,
                TemporalSampling::jitter(pendingIndex_, phases),
                pendingIndex_,
                reset};
    }
    // A key must identify one draw across frames (including entity generation
    // and submesh). Change geometryRevision if vertex positions change without
    // a corresponding previous-vertex stream. Such geometry gets no history.
    Object object(std::uint64_t key, std::uint64_t geometryRevision, const Mat4& model) {
        return recordObject(key, geometryRevision, model, nullptr).transform;
    }
    // The supplied mesh uses the same vertex order each frame. Store its pose
    // per view and transaction, not in the renderer's shared animation cache.
    DeformedObject deformedObject(std::uint64_t key,
                                  std::uint64_t geometryRevision,
                                  const Mat4& model,
                                  const MeshData& mesh) {
        if (mesh.topology != MeshData::Topology::Triangles || mesh.vertices.size() % 3) {
            throw std::invalid_argument("Invalid temporal deformed geometry");
        }
        return recordObject(key, geometryRevision, model, &mesh);
    }
    void commit() {
        if (!inFrame_) {
            throw std::logic_error("No temporal frame in progress");
        }
        committedConfig_ = pendingConfig_;
        committedViewProjection_ = pendingViewProjection_;
        committedIndex_ = pendingIndex_;
        committedObjects_ = std::move(pendingObjects_);
        valid_ = true;
        inFrame_ = false;
    }
    void discard() noexcept {
        pendingObjects_.clear();
        inFrame_ = false;
    }
    void invalidate() noexcept {
        discard();
        committedObjects_.clear();
        valid_ = false;
    }

  private:
    struct Record {
        Mat4 model;
        std::uint64_t geometryRevision;
        std::optional<MeshData> mesh;
    };
    DeformedObject recordObject(std::uint64_t key,
                                std::uint64_t geometryRevision,
                                const Mat4& model,
                                const MeshData* mesh) {
        if (!inFrame_) {
            throw std::logic_error("No temporal frame in progress");
        }
        if (!finite(model)) {
            throw std::invalid_argument("Invalid temporal model matrix");
        }
        Record record{model, geometryRevision, std::nullopt};
        if (mesh) {
            record.mesh = *mesh;
        }
        if (!pendingObjects_.emplace(key, std::move(record)).second) {
            throw std::invalid_argument("Duplicate temporal draw key");
        }
        const auto previous = committedObjects_.find(key);
        bool hasHistory = !pendingReset_ && previous != committedObjects_.end() &&
                          previous->second.geometryRevision == geometryRevision;
        if (hasHistory) {
            const auto& oldMesh = previous->second.mesh;
            hasHistory = bool(oldMesh) == bool(mesh) &&
                         (!mesh || (oldMesh->topology == mesh->topology &&
                                    oldMesh->vertices.size() == mesh->vertices.size()));
        }
        const Mat4 current = multiply(pendingViewProjection_, model);
        Object object{current,
                      hasHistory ? multiply(committedViewProjection_, previous->second.model)
                                 : current,
                      hasHistory};
        return {object, hasHistory && mesh ? &*previous->second.mesh : nullptr};
    }
    static bool same(const Configuration& a, const Configuration& b) {
        return a.render == b.render && a.display == b.display && a.scene == b.scene &&
               a.camera == b.camera && a.projection == b.projection;
    }
    static bool finite(const Mat4& m) {
        for (float value : m.m) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
        return true;
    }
    static void validate(const Configuration& c, const Mat4& vp) {
        if (!c.render.width || !c.render.height || !c.display.width || !c.display.height ||
            c.render.width > c.display.width || c.render.height > c.display.height || !finite(vp)) {
            throw std::invalid_argument("Invalid temporal frame configuration");
        }
        (void)TemporalSampling::phaseCount(c.render.width, c.display.width);
    }
    Configuration committedConfig_{}, pendingConfig_{};
    Mat4 committedViewProjection_{}, pendingViewProjection_{};
    std::map<std::uint64_t, Record> committedObjects_, pendingObjects_;
    std::uint64_t committedIndex_ = 0, pendingIndex_ = 0;
    bool valid_ = false, inFrame_ = false, pendingReset_ = true;
};
