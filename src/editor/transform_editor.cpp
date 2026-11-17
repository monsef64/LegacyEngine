// Transform editing maths. The gizmo itself is drawn and dragged by the Qt
// Scene view (src/ui/qt/qt_gizmo.cpp); this file keeps the parts that are
// independent of the toolkit: the tool/space/snap state, the held-body
// protocol, and the bounds used to centre the gizmo.
#include "editor/transform_editor.hpp"
#include "core/hierarchy.hpp"
#include "models/model_asset.hpp"

Vec3 TransformEditor::selectionCenter(World& world, Entity selected, const AssetManager* assets) {
    if (assets && (boundsRoot_ != assets->root || boundsRevision_ != modelAssetRevision)) {
        modelBounds_.clear();
        boundsRoot_ = assets->root;
        boundsRevision_ = modelAssetRevision;
    }
    bool haveMeshes = false, havePoints = false;
    Vec3 low{}, high{}, pointLow{}, pointHigh{};
    auto include = [](Vec3 p, Vec3& a, Vec3& b, bool& any) {
        if (!any) {
            a = b = p;
            any = true;
        }
        a = {std::min(a.x, p.x), std::min(a.y, p.y), std::min(a.z, p.z)};
        b = {std::max(b.x, p.x), std::max(b.y, p.y), std::max(b.z, p.z)};
    };
    for (auto entity : world.entities()) {
        auto ancestor = entity;
        unsigned depth = 0;
        while (ancestor != selected && ancestor != Entity{} && depth++ < 128) {
            ancestor = getParent(world, ancestor);
        }
        if (ancestor != selected || !world.has<Transform>(entity)) {
            continue;
        }
        const auto& pose = world.get<Transform>(entity);
        if (entity != selected) {
            include(pose.position, pointLow, pointHigh, havePoints);
        }
        const auto* mesh = world.tryGet<MeshRenderer>(entity);
        if (!mesh) {
            continue;
        }
        Bounds bounds;
        bool valid = false;
        if (!mesh->model.empty() && assets) {
            auto it = modelBounds_.find(mesh->model);
            if (it == modelBounds_.end()) {
                try {
                    const auto data = loadModelAsset(*assets, mesh->model);
                    for (const auto& vertex : data.vertices) {
                        include(vertex.position, bounds.low, bounds.high, bounds.valid);
                    }
                } catch (const std::exception&) {
                } // Invalid models still allow origin/child editing.
                if (modelBounds_.size() >= 4096) {
                    modelBounds_.clear();
                }
                it = modelBounds_.emplace(mesh->model, bounds).first;
            }
            bounds = it->second;
            valid = bounds.valid;
        } else if (meshBounds) {
            valid = meshBounds(mesh->mesh, bounds.low, bounds.high);
        }
        if (valid) {
            for (int i = 0; i < 8; ++i) {
                const Vec3 p{i & 1 ? bounds.high.x : bounds.low.x,
                             i & 2 ? bounds.high.y : bounds.low.y,
                             i & 4 ? bounds.high.z : bounds.low.z};
                include(pose.position + rotateVector(pose.rotation, p * pose.scale),
                        low,
                        high,
                        haveMeshes);
            }
        }
    }
    return haveMeshes   ? (low + high) * .5f
           : havePoints ? (pointLow + pointHigh) * .5f
                        : world.get<Transform>(selected).position;
}
