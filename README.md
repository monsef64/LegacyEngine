# LegacyEngine

A C++17 3D editor built with SDL3, OpenGL, Dear ImGui, ReactPhysics3D, miniaudio and Lua 5.4.
LegacyScript (`.ls`) adds Inspector-exported fields and engine APIs on top of Lua.

![LegacyEngine editor](docs/screenshot.png)

## Highlights

- Dockable Scene/Game views, Hierarchy, Inspector, Asset Browser and scripting panels.
- GameObjects, physics, cameras, audio, dynamic lights and filtered shadows.
- Project-owned materials, textures, six-face/HDR skyboxes and JSON projects.
- LegacyScript scripting and a VS Code extension with autocomplete and an LSP server.
- Play/Pause/Stop, undo/redo, native project dialogs and keyboard/mouse/gamepad input.

## Build

Requires a C++17 compiler, CMake 3.16+, Git and desktop/OpenGL development libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/LegacyEngine
```

A graphical session and OpenGL 3.3 driver are required. See the
[build guide](docs/building.md) for dependencies and graphical tests.

## Documentation and downloads

Read the **[documentation](docs/index.md)** for editor guides, component settings,
LegacyScript examples, APIs and limitations. Detailed usage lives there rather
than in this README.

The [Release workflow](.github/workflows/release.yml) publishes the engine,
**legacyscript-vsix**, and **legacyengine-docs** artifacts in the same run.
Extract the docs artifact and open `index.html` offline. Engine packages also
include the HTML docs; tagged releases attach a docs ZIP and the VSIX.
See [documentation build instructions](docs/index.md#build-this-documentation)
and the [VS Code extension guide](docs/vscode.md).

## License

**GNU General Public License, version 2 only** (`GPL-2.0-only`). See [LICENSE](LICENSE).
Third-party dependencies and assets retain their respective licenses and notices.
