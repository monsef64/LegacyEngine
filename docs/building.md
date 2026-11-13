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

For headless graphical checks on Linux:

```sh
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a build/LegacyEngine --smoke-test
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1600x900x24' \
  build/LegacyEngine --screenshot build/editor.bmp
```

Both modes finish after 120 rendered frames. `--screenshot` captures the actual
OpenGL/ImGui framebuffer as BMP and ignores the saved layout for a reproducible view.
CI converts this to PNG and uploads it as **editor-screenshot**. A manual run can
also enable **update_screenshot** to commit the captured image back to its selected
branch; ordinary builds never modify the repository.
