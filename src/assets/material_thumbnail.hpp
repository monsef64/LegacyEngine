#pragma once
#include "assets/texture_thumbnail.hpp"
#include "materials/material.hpp"

// Shared CPU preview used by graphics backends; keep material thumbnails
// visually identical while uploading them through the selected renderer.
inline TextureThumbnail materialThumbnail(const AssetManager& assets,
                                          const MaterialProperties& material) {
    TextureThumbnail image, albedo;
    bool textured = false;
    if (!material.texture.empty() &&
        std::filesystem::is_regular_file(assets.resolve(material.texture))) {
        albedo = loadTextureThumbnail(assets, material.texture);
        textured = true;
    }
    for (int y = 0; y < TextureThumbnail::size; ++y) {
        for (int x = 0; x < TextureThumbnail::size; ++x) {
            const float nx = (x + .5f) / TextureThumbnail::size * 2 - 1;
            const float ny = 1 - (y + .5f) / TextureThumbnail::size * 2;
            const float r = nx * nx + ny * ny;
            const auto i = (y * TextureThumbnail::size + x) * 4;
            const float background = ((x / 8 + y / 8) % 2) ? .16f : .12f;
            Vec3 color{background, background, background};
            if (r < .9f) {
                const float nz = std::sqrt(1 - r / .9f);
                const float light = .18f + .82f * std::max(0.f, (-nx + ny + nz) * .577f);
                const float spec =
                    material.specular * std::pow(std::max(0.f, (-nx + ny + nz) * .577f), 24.f);
                Vec3 tex{1, 1, 1};
                if (textured) {
                    tex = {albedo.pixels[i] / 255.f,
                           albedo.pixels[i + 1] / 255.f,
                           albedo.pixels[i + 2] / 255.f};
                }
                color = {material.color.x * tex.x * light + spec,
                         material.color.y * tex.y * light + spec,
                         material.color.z * tex.z * light + spec};
            }
            image.pixels[i] = static_cast<unsigned char>(std::clamp(color.x, 0.f, 1.f) * 255);
            image.pixels[i + 1] = static_cast<unsigned char>(std::clamp(color.y, 0.f, 1.f) * 255);
            image.pixels[i + 2] = static_cast<unsigned char>(std::clamp(color.z, 0.f, 1.f) * 255);
            image.pixels[i + 3] = 255;
        }
    }
    return image;
}
