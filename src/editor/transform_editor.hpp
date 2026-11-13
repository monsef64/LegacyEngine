#pragma once

#include "core/ecs.hpp"
#include "editor/editor_layout.hpp"
#include "editor/transform_edit.hpp"
#include "scene/camera.hpp"
#include <ImGuizmo.h>
#include <imgui.h>

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
    }
    void drawToolbar();
    void draw(World& world,
              Entity entity,
              bool enabled,
              const OrbitCamera& camera,
              float aspect,
              const EditorRect* scene = nullptr,
              ImDrawList* drawList = nullptr);

  private:
    bool localSpace_ = false;
    TransformTool tool_ = TransformTool::Move;
    bool hovered_ = false, dragging_ = false, heldBody_ = false;
    Entity heldEntity_;
    RigidBody savedBody_;
    Vec3 scaleAtStart_{1, 1, 1};
};
