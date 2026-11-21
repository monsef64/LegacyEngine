#pragma once
// Project-authored default plus a Play-only override. Never serialize the override.
namespace TextureQuality {
inline int project = 0, runtime = -1;
inline bool playing = false;
inline int current() {
    return playing && runtime >= 0 ? runtime : project;
}
inline void lifecycle(bool active) {
    playing = active;
    if (!active) {
        runtime = -1;
    }
}
} // namespace TextureQuality
