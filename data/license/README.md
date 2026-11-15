# Third-party licenses and notices

This directory is shipped with the engine as `data/license/`. The engine's own
license remains in the repository/package root `LICENSE`.

## Direct dependencies and assets

| Component | Source/version used by this checkout | Notice |
| --- | --- | --- |
| SDL | release-3.4.20 (CMake default) | SDL3.txt |
| ReactPhysics3D | v0.10.2 | ReactPhysics3D.txt |
| Assimp | v5.4.3 | assimp.txt |
| Dear ImGui | v1.92.5-docking | DearImGui.txt |
| ImGuizmo | 105e4fde45e4e74aae8973d98750820ef7aca7e2 | ImGuizmo.txt |
| Lua | v5.4.8 | Lua.txt |
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
(Lua, pugixml, MiniZip, miniz, Open3DGC, and Assimp's stb_image) are extracted
without rewriting their terms. Apache-2.0.txt provides the full Apache license text for simdjson.

## Refreshing

After configuring CMake so the pinned dependencies exist under `<build>/_deps`, run:

```sh
python3 tools/collect_licenses.py <build>
```

Release packaging runs this command before copying `data/`. If dependencies or
vendored assets change, review this index and any new embedded notices as well.
A build using a system-provided SDL may need the corresponding distributor's
additional notices; the checked-in SDL notice matches the default vendored build.
