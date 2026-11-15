# Build and test
Requires a C++17 compiler, CMake 3.20+, Git, Vulkan headers and loader, a Vulkan
shader compiler (`glslangValidator` or `glslang`), and platform development libraries.
CMake fetches SDL3 (if not installed), Jolt Physics, Dear ImGui, ImGuizmo and Lua.
Roboto/Font Awesome fonts, miniaudio, simdjson, nlohmann/json and stb_image
are included under `third_party/`.

On Debian/Ubuntu, install the window/audio and Vulkan development dependencies used
in [the engine workflow](../.github/workflows/release.yml). Then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/LegacyEngine
```

Use `-DLEGACY_VENDORED_SDL=ON` to force a static vendored SDL build.
A graphical session, Vulkan loader and Vulkan-capable driver are required at runtime.
Native file dialogs require a working desktop portal or another SDL dialog backend.
Linux CI builds on Ubuntu 24.04; check its run log for the resulting glibc requirement.
GitHub Actions builds/packages only, with `BUILD_TESTING=OFF`; tests are manual.
Use `-DBUILD_TESTING=ON` locally to build the optional test executables.

For graphical checks, run `build/LegacyEngine --smoke-test` with a Vulkan-capable
SDL video driver. A software Vulkan implementation can be used when available;
for example, SDL's offscreen backend works with SwiftShader. The smoke test
runs 120 frames and exercises the Scene, Game, camera preview, a secondary game
window, resize and VSync recreation. To include it in CTest, configure with
`-DLEGACY_TEST_EDITOR_SMOKE=ON` on a machine with a usable Vulkan video backend.
Screenshot capture is not part of the test.

Vulkan GLSL sources live in `data/shaders/vulkan/`. CMake compiles SPIR-V and
copies it to `data/shaders/vulkan/compiled/` beside the executable. Keep that
directory when moving or packaging binaries; shader files are loaded from the
executable's directory, not the working directory. Missing or invalid shaders
are reported as initialization errors.

## Offline scripting reference

The release engine package includes `docs/index.html` and
`docs/scripting-reference.html`. Open **Help > Documentation** or
**Help > Scripting Reference** in the engine; both work offline and locate the
manual beside the executable, even after opening a project elsewhere.

For a local build, install `docs/requirements.txt` then run
`python docs/build.py --output build/docs` beside `build/LegacyEngine`.
If changing scripting APIs, update `scripts/definitions/legacyengine.lua` and
`docs/generate_reference.py` examples, then run
`python docs/generate_reference.py`. Documentation tests reject a stale reference
or an API without an example. The release workflow builds the complete HTML site,
includes it in the engine package, and publishes a standalone docs ZIP.
