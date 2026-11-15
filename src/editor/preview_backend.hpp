#pragma once
#include "assets/assets.hpp"
#include "core/ecs.hpp"
#include "rendering/mesh.hpp"
#include <imgui.h>

// Interface used by model and animation editors. A backend owns independent
// images for the two slots and must return a current ImGui texture each frame.
class EditorPreviewBackend {
  public:
    enum class Slot { Animation, Model };
    struct Image {
        ImTextureID texture = ImTextureID_Invalid;
        bool flip = false;
    };
    virtual ~EditorPreviewBackend() = default;
    virtual bool meshBounds(MeshHandle handle, Vec3& low, Vec3& high) const = 0;
    virtual bool
    modelBounds(const AssetManager& assets, const std::string& path, Vec3& low, Vec3& high) = 0;
    virtual Image image(Slot slot,
                        World& world,
                        const AssetManager& assets,
                        const Mat4& vp,
                        Vec3 eye,
                        int width,
                        int height,
                        bool redraw) = 0;
};
