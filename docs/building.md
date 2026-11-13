# Build and test
Requires a C++17 compiler, CMake 3.16+, Git and platform development libraries.
CMake fetches SDL3 (if not installed), ReactPhysics3D, Dear ImGui, ImGuizmo and Lua.
GLAD, Roboto/Font Awesome fonts, miniaudio, simdjson, nlohmann/json and stb_image
are included under `third_party/`.

On Debian/Ubuntu, install the window/audio/OpenGL development dependencies used
in [the engine workflow](../.github/workflows/release.yml). Then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/LegacyEngine
```

Use `-DLEGACY_VENDORED_SDL=ON` to force a static vendored SDL build.
A graphical session and an OpenGL 3.3-capable driver are required at runtime.
Native file dialogs require a working desktop portal or another SDL dialog backend.
Linux CI builds on Ubuntu 24.04; check its run log for the resulting glibc requirement.
GitHub Actions builds/packages only, with `BUILD_TESTING=OFF`; tests are manual.
Use `-DBUILD_TESTING=ON` locally to build the optional test executables.

For headless graphical checks on Linux:

```sh
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a build/LegacyEngine --smoke-test
```

Smoke tests finish after 120 rendered frames. Screenshot capture is no longer
part of the editor or Actions artifacts.

Engine GLSL sources live in `data/shaders/` (lit, unlit/shadow, and skybox vertex
and fragment files). CMake copies `data/` beside the executable, and release
archives include it. Keep that directory beside the engine when moving binaries;
shader files are loaded from the executable's directory, not the working directory.
Missing shader data is reported as an initialization/render error. Edit these
files and rebuild to refresh the runtime copy; no C++ shader strings are maintained.

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
