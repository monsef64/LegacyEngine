# Vulkan migration status

The production `LegacyEngine` executable uses Vulkan for its main ImGui frame,
Scene, Game and camera views, editor overlays, thumbnails and asset/model/animation
previews. A separate game window uses a shared-device secondary swapchain. The
renderer and graphics-specific build dependencies use Vulkan.

Feature parity verification and platform validation are not complete.

## Build and run

Install Vulkan headers, a Vulkan loader and `glslangValidator` (or `glslang`),
in addition to the platform build dependencies in [building.md](building.md).

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build -R '^vulkan_' --output-on-failure
./build/LegacyEngine --smoke-test
```

CMake compiles the GLSL under `data/shaders/vulkan/` and copies SPIR-V into
`data/shaders/vulkan/compiled/` next to the engine executable. Runtime packages
must include that directory. The editor uses the full lit and shadow shaders;
model and asset previews use a no-shadow lit variant.

## Coverage and limitations

The 120-frame editor smoke test exercises Scene, Game and camera views, a
separate game window, resize and VSync recreation. The Vulkan tests additionally
exercise image readback, materials, light types, shadow maps, skyboxes, animated
meshes, ImGui, thumbnails, previews and overlay geometry. The main editor and
tests have run on SDL's offscreen backend with SwiftShader. Real GPU drivers,
validation layers, multiple platforms and interactive visual parity still need
verification.

The Vulkan renderer serializes significant work on the graphics queue; it is a
correctness-first implementation and not yet optimized. Animation currently
skins on the CPU and uploads Vulkan vertex buffers; the compute shader is tested
but not used by the runtime. Separate-window ImGui presentation waits for the
shared upload buffers to become idle. The secondary context must be destroyed
before its owner and requires a compatible surface format and present queue.

The full test suite currently has three failures outside the Vulkan tests:
`editor_layout`, `project_io`, and `audio_editor`. Do not treat the migration as
complete until these are investigated and the release/CI workflows are
verified on supported platforms.

## Coordinate conventions

Existing engine camera and shadow matrices produce clip depth in `[-w, w]`.
`vulkanClipMatrix()` converts them to Vulkan depth `[0, w]` and flips Y for a
positive-height viewport. The renderer uses counter-clockwise front faces for
normal meshes and clockwise for mirrored meshes. CPU frustum culling against
converted matrices uses `ViewFrustum::DepthRange::ZeroToOne`.
