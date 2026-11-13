#pragma once
#include <algorithm>
struct ShadowSettings {
    int mode = 0;          // 0: directional CSM (primary); 1: single-map PCF compatibility path.
    int resolution = 1024; // Atlas side, per layer. Light quality scales this down.
    bool valid() const {
        return (mode == 0 || mode == 1) &&
               (resolution == 256 || resolution == 512 || resolution == 1024 || resolution == 2048);
    }
    int lightResolution(int quality) const {
        return quality <= 0 ? 0 : std::max(64, resolution >> (3 - std::clamp(quality, 1, 3)));
    }
};
