#pragma once

#include "assets/assets.hpp"
#include "core/ecs.hpp"
#include "editor/editor_layout.hpp"
#include "editor/transform_edit.hpp"
#include "scene/camera.hpp"
#include <ImGuizmo.h>
#include <functional>
#include <imgui.h>
#include <map>

// Owns an edit gesture. Dynamic objects become obstacles during the drag;
// their original mass/gravity settings are restored at rest on release.
class TransformEditor {
  public:
    bool capturesMouse() const {
        return hovered_ || dragging_;
    }
    void setTool(TransformTool tool) {
        if (!dragging_) {
            tool_ = tool;
        }
    }
    bool localSpace() const {
        return localSpace_;
    }
    void setLocalSpace(bool value) {
        if (!dragging_) {
            localSpace_ = value;
        }
    }
    bool dragging() const {
        return dragging_;
    }
    void finish(World& world) {
        ImGuizmo::Enable(false); // cancel any internal gesture on focus/mode/selection changes
        if (heldBody_ && world.alive(heldEntity_)) {
            world.add(heldEntity_, savedBody_);
        }
        heldBody_ = false;
        heldEntity_ = {};
        dragging_ = hovered_ = false;
        face_ = -1;
    }
    std::function<bool(MeshHandle, Vec3&, Vec3&)> meshBounds;
    void drawToolbar();
    void draw(World& world,
              Entity entity,
              bool enabled,
              const OrbitCamera& camera,
              float aspect,
              const EditorRect* scene = nullptr,
              ImDrawList* drawList = nullptr,
              const AssetManager* assets = nullptr,
              bool navigationOnly = false);

  private:
    bool drawColliderFaces(World& world,
                           Entity entity,
                           const OrbitCamera& camera,
                           float aspect,
                           const EditorRect& scene,
                           ImDrawList* drawList);
    int face_ = -1;
    Collider faceStart_;
    Transform faceTransform_;
    Vec3 faceOrigin_, faceAxis_;
    float faceParameter_ = 0;
    Vec3 selectionCenter(World& world, Entity selected, const AssetManager* assets);
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
