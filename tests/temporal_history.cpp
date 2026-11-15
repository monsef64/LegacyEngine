#include "rendering/temporal_history.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
Mat4 translated(float x, float y = 0) {
    Mat4 m{};
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1;
    m.m[12] = x;
    m.m[13] = y;
    return m;
}
bool same(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (std::abs(a.m[i] - b.m[i]) > 1e-6f) {
            return false;
        }
    }
    return true;
}
} // namespace
int main() {
    TemporalHistory a, b;
    TemporalHistory::Configuration cfg{{640, 360}, {1280, 720}, 1, 2, 3};
    auto frame = a.begin(cfg, translated(1));
    assert(frame.reset && frame.index == 0);
    auto obj = a.object(10, 1, translated(2));
    assert(!obj.hasHistory && same(obj.currentViewProjectionModel, translated(3)) &&
           same(obj.previousViewProjectionModel, translated(3)));
    a.commit();
    frame = a.begin(cfg, translated(3));
    assert(!frame.reset && frame.index == 1 && same(frame.previousViewProjection, translated(1)));
    obj = a.object(10, 1, translated(5));
    assert(obj.hasHistory && same(obj.currentViewProjectionModel, translated(8)) &&
           same(obj.previousViewProjectionModel, translated(3)));
    auto firstAppearance = a.object(11, 1, translated(7));
    assert(!firstAppearance.hasHistory && same(firstAppearance.currentViewProjectionModel,
                                               firstAppearance.previousViewProjectionModel));
    bool duplicate = false;
    try {
        a.object(10, 1, translated(0));
    } catch (const std::invalid_argument&) {
        duplicate = true;
    }
    assert(duplicate);
    a.discard();
    // Retrying a failed render uses the last committed frame, not the failed frame.
    frame = a.begin(cfg, translated(4));
    obj = a.object(10, 1, translated(6));
    assert(frame.index == 1 && same(frame.previousViewProjection, translated(1)) &&
           obj.hasHistory && same(obj.previousViewProjectionModel, translated(3)));
    a.commit();
    frame = a.begin(cfg, translated(5));
    obj = a.object(10, 2, translated(8));
    assert(!obj.hasHistory &&
           same(obj.previousViewProjectionModel, obj.currentViewProjectionModel));
    // A draw absent from the previous frame must not reuse an older pose.
    firstAppearance = a.object(11, 1, translated(9));
    assert(!firstAppearance.hasHistory);
    a.commit();
    // Histories belong to individual views, even if they share the same world.
    frame = b.begin(cfg, translated(10));
    assert(frame.reset && !b.object(10, 2, translated(8)).hasHistory);
    b.commit();
    frame = a.begin(cfg, translated(6), true);
    assert(frame.reset && frame.index == 0 && !a.object(10, 2, translated(8)).hasHistory);
    a.commit();
    cfg.display = {1920, 1080};
    frame = a.begin(cfg, translated(7));
    assert(frame.reset && !a.object(10, 2, translated(8)).hasHistory);
    a.commit();
    cfg.camera = 4;
    frame = a.begin(cfg, translated(8));
    assert(frame.reset);
    a.commit();
    a.invalidate();
    frame = a.begin(cfg, translated(9));
    assert(frame.reset);
    a.discard();
    auto invalid = cfg;
    invalid.render.width = 0;
    bool rejected = false;
    try {
        a.begin(invalid, translated(0));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    auto bad = translated(0);
    bad.m[3] = std::numeric_limits<float>::quiet_NaN();
    rejected = false;
    try {
        a.begin(cfg, bad);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    // Deformation history is per view and only advances on commit.
    TemporalHistory skin, other;
    MeshData pose;
    pose.vertices.resize(3);
    skin.begin(cfg, translated(0));
    assert(!skin.deformedObject(20, 100, translated(0), pose).transform.hasHistory);
    skin.commit();
    pose.vertices[0].position.x = 1;
    skin.begin(cfg, translated(0));
    auto deformation = skin.deformedObject(20, 100, translated(0), pose);
    assert(deformation.transform.hasHistory && deformation.previousMesh &&
           deformation.previousMesh->vertices[0].position.x == 0);
    skin.discard();
    other.begin(cfg, translated(0));
    assert(!other.deformedObject(20, 100, translated(0), pose).transform.hasHistory);
    other.commit();
    skin.begin(cfg, translated(0));
    deformation = skin.deformedObject(20, 100, translated(0), pose);
    assert(deformation.previousMesh && deformation.previousMesh->vertices[0].position.x == 0);
    skin.commit();
    skin.begin(cfg, translated(0));
    assert(!skin.object(20, 100, translated(0)).hasHistory); // deformed -> rigid
    skin.commit();
    skin.begin(cfg, translated(0));
    assert(!skin.deformedObject(20, 100, translated(0), pose).transform.hasHistory);
    skin.commit();
    pose.vertices.resize(6);
    skin.begin(cfg, translated(0));
    assert(!skin.deformedObject(20, 100, translated(0), pose).transform.hasHistory);
    skin.discard();
}
