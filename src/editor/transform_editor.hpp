#pragma once

#include "assets/assets.hpp"
#include "core/ecs.hpp"
#include "editor/editor_geometry.hpp"
#include "editor/transform_edit.hpp"
#include "scene/camera.hpp"
#include <functional>
#include <map>

// Owns an edit gesture. Dynamic objects become obstacles during the drag;
// their original mass/gravity settings are restored at rest on release.
class TransformEditor {
  public:
    bool capturesMouse() const { return hovered_ || dragging_; }
    void setTool(TransformTool tool) {
        if (!dragging_ && tool_ != tool) {
            tool_ = tool;
        }
    }
    TransformTool tool() const { return tool_; }
    bool localSpace() const { return localSpace_; }
    void setLocalSpace(bool value) {
        if (!dragging_ && localSpace_ != value) {
            localSpace_ = value;
        }
    }
    bool centerPivot() const { return centerPivot_; }
    void setCenterPivot(bool value) { centerPivot_ = value; }
    float snapStep() const { return snap_; }
    void setSnapStep(float value) {
        snap_ = std::isfinite(value) ? std::clamp(std::round(value * 4) / 4, .25f, 10000.f) : .5f;
    }
    bool hovered() const { return hovered_; }
    bool dragging() const { return dragging_; }
    void finish(World& world) {
        if (heldBody_ && world.alive(heldEntity_)) {
            world.add(heldEntity_, savedBody_);
        }
        heldBody_ = false;
        heldEntity_ = {};
        dragging_ = hovered_ = false;
    }
    std::function<bool(MeshHandle, Vec3&, Vec3&)> meshBounds;
    // The pivot the gizmo is drawn at: the centre of the selection's bounds,
    // including any children, so a group of objects drags as one.
    Vec3 selectionCenter(World& world, Entity selected, const AssetManager* assets = nullptr);

  private:
    // Collider face dragging lived in the old UI draw path; the Qt gizmo
    // edits the collider directly, so only the state below is still shared.
    struct Bounds {
        Vec3 low, high;
        bool valid = false;
    };
    std::map<std::string, Bounds> modelBounds_;
    std::filesystem::path boundsRoot_;
    unsigned boundsRevision_ = 0;
    bool centerPivot_ = false;
    float snap_ = 0.5f;
    Vec3 pivotLocal_{};
    bool localSpace_ = false;
    TransformTool tool_ = TransformTool::Move;
    bool hovered_ = false, dragging_ = false, heldBody_ = false;
    Entity heldEntity_;
    RigidBody savedBody_;
    Vec3 scaleAtStart_{1, 1, 1};
};
