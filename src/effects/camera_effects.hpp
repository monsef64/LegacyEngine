#pragma once
#include <array>
#include <cmath>
struct CameraEffects {
    bool enabled = true, bloom = false, colorGrading = false, vignette = false,
         depthOfField = false, motionBlur = false, filmGrain = false, chromaticAberration = false,
         antiAliasing = false, ambientOcclusion = false, nightVision = false, nausea = false;
    float bloomIntensity = .3f, bloomThreshold = 1, exposure = 0, contrast = 1, saturation = 1,
          temperature = 0, vignetteIntensity = .3f, focusDistance = 10, focusRange = 5,
          dofStrength = .5f, motionStrength = .5f, grainIntensity = .03f,
          aberrationStrength = .002f, aoIntensity = .5f, aoRadius = .5f, nightVisionMode = 0,
          nightVisionIntensity = 1, nauseaIntensity = .3f, nauseaSpeed = 1;
};
namespace CameraFx {
struct BoolField {
    const char* name;
    const char* label;
    bool CameraEffects::* member;
};
struct FloatField {
    const char* name;
    const char* label;
    float CameraEffects::* member;
    float low, high;
};
inline constexpr std::array<BoolField, 12> boolFields{
    {{"enabled", "Enabled", &CameraEffects::enabled},
     {"bloom", "Bloom", &CameraEffects::bloom},
     {"colorGrading", "Color grading", &CameraEffects::colorGrading},
     {"vignette", "Vignette", &CameraEffects::vignette},
     {"depthOfField", "Depth of field", &CameraEffects::depthOfField},
     {"motionBlur", "Motion blur (camera)", &CameraEffects::motionBlur},
     {"filmGrain", "Film grain", &CameraEffects::filmGrain},
     {"chromaticAberration", "Chromatic aberration", &CameraEffects::chromaticAberration},
     {"antiAliasing", "Anti-aliasing (FXAA)", &CameraEffects::antiAliasing},
     {"ambientOcclusion", "Ambient occlusion", &CameraEffects::ambientOcclusion},
     {"nightVision", "Night vision", &CameraEffects::nightVision},
     {"nausea", "Nausea", &CameraEffects::nausea}}};
inline constexpr std::array<FloatField, 19> floatFields{
    {{"bloomIntensity", "Bloom intensity", &CameraEffects::bloomIntensity, 0, 4},
     {"bloomThreshold", "Bloom threshold", &CameraEffects::bloomThreshold, 0, 8},
     {"exposure", "Exposure (stops)", &CameraEffects::exposure, -5, 5},
     {"contrast", "Contrast", &CameraEffects::contrast, 0, 2},
     {"saturation", "Saturation", &CameraEffects::saturation, 0, 2},
     {"temperature", "Temperature", &CameraEffects::temperature, -1, 1},
     {"vignetteIntensity", "Vignette intensity", &CameraEffects::vignetteIntensity, 0, 1},
     {"focusDistance", "Focus distance (m)", &CameraEffects::focusDistance, .01f, 10000},
     {"focusRange", "Focus range (m)", &CameraEffects::focusRange, .01f, 10000},
     {"dofStrength", "DOF strength", &CameraEffects::dofStrength, 0, 1},
     {"motionStrength", "Motion strength", &CameraEffects::motionStrength, 0, 1},
     {"grainIntensity", "Grain intensity", &CameraEffects::grainIntensity, 0, .2f},
     {"aberrationStrength", "Aberration strength", &CameraEffects::aberrationStrength, 0, .02f},
     {"aoIntensity", "AO intensity", &CameraEffects::aoIntensity, 0, 2},
     {"aoRadius", "AO radius (m)", &CameraEffects::aoRadius, .01f, 5},
     {"nightVisionMode", "Vision mode", &CameraEffects::nightVisionMode, 0, 1},
     {"nightVisionIntensity", "Vision intensity", &CameraEffects::nightVisionIntensity, 0, 1},
     {"nauseaIntensity", "Nausea intensity", &CameraEffects::nauseaIntensity, 0, 1},
     {"nauseaSpeed", "Nausea speed", &CameraEffects::nauseaSpeed, 0, 5}}};
inline bool valid(const CameraEffects& c) {
    for (const auto& f : floatFields) {
        auto v = c.*f.member;
        if (!std::isfinite(v) || v < f.low || v > f.high) {
            return false;
        }
    }
    return true;
}
inline int mask(const CameraEffects& c) {
    return !c.enabled ? 0
                      : (c.bloom ? 1 : 0) | (c.colorGrading ? 2 : 0) | (c.vignette ? 4 : 0) |
                            (c.depthOfField ? 8 : 0) | (c.motionBlur ? 16 : 0) |
                            (c.filmGrain ? 32 : 0) | (c.chromaticAberration ? 64 : 0) |
                            (c.antiAliasing ? 128 : 0) | (c.ambientOcclusion ? 256 : 0) |
                            (c.nightVision ? 512 : 0) | (c.nausea ? 1024 : 0);
}
} // namespace CameraFx
