#include "rendering/temporal_sampling.hpp"
#include "rendering/vulkan_clip.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace TemporalSampling;
namespace {
bool close(float a, float b) {
    return std::abs(a - b) < .00001f;
}
struct Clip {
    float x, y, z, w;
};
Clip project(const Mat4& m, Vec3 p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14],
            m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15]};
}
void checkProjection(const Mat4& vp, Vec3 position) {
    const Extent extent{1280, 720};
    const Offset sample{.25f, -.375f};
    const auto before = project(vulkanClipMatrix(vp), position);
    const auto after =
        project(vulkanClipMatrix(jitterViewProjection(vp, sample, extent)), position);
    // Positive-height Vulkan viewport maps NDC to pixels in the same direction;
    // Vulkan's clip conversion inverts the engine Y axis; both jitter
    // components must shift geometry by the requested framebuffer pixels.
    assert(close((after.x / after.w - before.x / before.w) * extent.width * .5f, sample.x));
    assert(close((after.y / after.w - before.y / before.w) * extent.height * .5f, sample.y));
    assert(close(before.z, after.z) && close(before.w, after.w));
}
} // namespace

int main() {
    const Extent display{1920, 1080};
    assert(renderExtent(display, Quality::NativeAA) == (Extent{1920, 1080}));
    assert(renderExtent(display, Quality::Quality) == (Extent{1280, 720}));
    assert(renderExtent(display, Quality::Balanced) == (Extent{1129, 635}));
    assert(renderExtent(display, Quality::Performance) == (Extent{960, 540}));
    assert(renderExtent(display, Quality::UltraPerformance) == (Extent{640, 360}));
    assert(renderExtent({1, 2}, Quality::UltraPerformance) == (Extent{1, 1}));
    assert(phaseCount(1920, 1920) == 8);
    assert(phaseCount(1280, 1920) == 18);
    assert(phaseCount(960, 1920) == 32);
    assert(phaseCount(640, 1920) == 72);
    auto first = jitter(0, 18), second = jitter(1, 18), repeated = jitter(18, 18);
    assert(close(first.x, 0) && close(first.y, -1.f / 6));
    assert(close(second.x, -.25f) && close(second.y, 1.f / 6));
    assert(close(first.x, repeated.x) && close(first.y, repeated.y));
    for (uint32_t i = 0; i < 72; ++i) {
        auto sample = jitter(i, 72);
        assert(sample.x >= -.5f && sample.x < .5f && sample.y >= -.5f && sample.y < .5f);
    }
    const auto view = lookAt({2, 3, 4}, {0, 0, 0}, {0, 1, 0});
    checkProjection(multiply(perspective(radians(70), 16.f / 9, .1f, 100.f), view), {0, 0, 0});
    Mat4 ortho{};
    ortho.m[0] = .5f;
    ortho.m[5] = .5f;
    ortho.m[10] = -.1f;
    ortho.m[15] = 1;
    checkProjection(multiply(ortho, view), {0, 0, 0});
    bool rejected = false;
    try {
        (void)renderExtent({0, 100}, Quality::Quality);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    rejected = false;
    try {
        (void)phaseCount(0, 100);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    rejected = false;
    try {
        (void)jitter(0, 0);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}
