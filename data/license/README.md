# Third-party licenses and notices

This directory is shipped with the engine as `data/license/`. The engine's own
license remains in the repository/package root `LICENSE`.

## Direct dependencies and assets

| Component | Source/version used by this checkout | Notice |
| --- | --- | --- |
| SDL | release-3.4.20 (CMake default) | SDL3.txt |
| Jolt Physics | v5.3.0 | Jolt.txt |
| Assimp | v5.4.3 | assimp.txt |
| meshoptimizer | v0.22 | meshoptimizer.txt |
| Recast Navigation / Detour | v1.6.0 | RecastNavigation.txt |
| Dear ImGui | v1.92.5-docking | DearImGui.txt |
| ImGuizmo | 105e4fde45e4e74aae8973d98750820ef7aca7e2 | ImGuizmo.txt |
| Lua | v5.4.8 | Lua.txt |
| RmlUi (core and SDL3/OpenGL3 adapters) | 6.1 / 58c751531f4c1a1eb2dc56f409b8e8a323a0c9ad | RmlUi.txt, RmlUi-containers.txt |
| FreeType (font rasterizer) | 2.13.3, used under the FreeType License (FTL) | FreeType.txt, freetype-bundled/ |
| glad and generated OpenGL/Khronos headers | glad 2.0.8 | glad.txt, Khronos.txt, Apache-2.0.txt |
| miniaudio | vendored `third_party/miniaudio` | miniaudio.txt |
| simdjson | vendored 3.12.3 | simdjson.txt |
| nlohmann/json | vendored `third_party/json` | nlohmann-json.txt |
| stb_image | vendored `third_party/stb` | stb.txt |
| Roboto font | vendored `third_party/roboto` | Roboto.txt |
| Font Awesome Free | vendored `third_party/fontawesome` | FontAwesome.txt |

`assimp-bundled/` includes notices supplied by Assimp's bundled dependencies,
including zlib, MiniZip, miniz/zip, pugixml, Open3DGC, stb, poly2tri, Clipper,
OpenDDLParser, RapidJSON, UTF8-CPP, and optional dependency sources. Some optional
or test-only sources (for example Draco and GoogleTest) are not enabled in the
engine build; retaining their upstream notices does not mean they are linked.

`sdl3-bundled/` contains SDL's bundled HIDAPI alternative-license notices and
yuv2rgb license. HIDAPI offers a choice of licenses; including its GPL alternative
text does not mean that alternative has been selected for the engine.

The upstream notice texts are preserved. A few source-embedded license comments
(Lua, Khronos, pugixml, MiniZip, miniz, Open3DGC, and Assimp's stb_image) are extracted
without rewriting their terms. Apache-2.0.txt provides the full Apache license text
also referenced by glad.

## Refreshing

After configuring CMake so the pinned dependencies exist under `<build>/_deps`, run:

```sh
python3 tools/collect_licenses.py <build>
```

Release packaging runs this command before copying `data/`. If dependencies or
vendored assets change, review this index and any new embedded notices as well.
A build using a system-provided SDL may need the corresponding distributor's
additional notices; the checked-in SDL notice matches the default vendored build.

Portions of this software are copyright © 2024 The FreeType Project
(<https://freetype.org>). All rights reserved. See FreeType.txt.
