# Experimental Game-only FSR 3 upscaling

FSR 3 upscaling is available as an **opt-in, Linux-only experimental build**.
It is off by default. In Project > Rendering, choose Native AA, Quality,
Balanced, Performance, or Ultra Performance under Game FSR. The status there
reports whether the Game view is using FSR or native fallback. Both embedded and
detached Game use the same output; Scene and camera/asset previews stay native.
Frame generation is not implemented.

The requested mode is persisted per project. Scripts can use
`Rendering.setGameFsrQuality(0..5)` and `Rendering.getGameFsrQuality()`; the
getter reports the requested mode, not whether the backend is active. Builds
without the optional backend always render natively.

FSR needs a Vulkan 1.2 device, compute and appropriate image formats and
subgroup operations. The current temporal inputs support full-viewport
perspective cameras and opaque triangle meshes, including CPU-skinned models.
Skyboxes, transparent meshes, lines, collider overlays and unsupported cameras
fall back to native Game rendering. Missing or changed object history triggers
an upscaler reset. Switching scenes/cameras/projection/modes, resize, camera
cuts and rendering gaps also reset history. Errors fall back to native rather
than changing Scene/preview rendering. The path waits for GPU work and uploads
previous vertex poses; it is a correctness-first experiment, not an optimized
implementation. Reactive/transparency masks and real-world visual quality are
not yet supported or validated.

## Building the optional backend

`tools/build_fsr3_experiment.sh` downloads two pinned, SHA-256-checked source
archives: AMD FidelityFX SDK 1.1.4 and the unaffiliated
[xplane-fsr3 Linux port](https://github.com/JonathanOrr/xplane-fsr3) at commit
`fa51577c9bc2c3477e64dfdd358796aebb9ca45b`. The port patches the SDK and
backports FSR 3.1.5. The download is roughly 190 MB and shader generation can be
expensive. No SDK or prebuilt binary is checked into this repository. The
script requires Linux, Bash, curl, tar, patch, Python 3, CMake, a C/C++ compiler,
Vulkan development headers and loader, and `glslangValidator` with SPIR-V
optimization enabled. `FSR3_BUILD_JOBS` and `FSR3_SHADER_JOBS` default to 2.
The work directory must not already exist; partial builds are not silently reused.

```sh
tools/build_fsr3_experiment.sh build/fsr3-experiment
cmake -S . -B build -DBUILD_TESTING=ON \
  -DLEGACY_FSR3_GAME=ON \
  -DLEGACY_FSR3_EXPERIMENT_DIR="$PWD/build/fsr3-experiment"
cmake --build build --target LegacyEngine vulkan_fsr3_game_tests
ctest --test-dir build -R '^vulkan_fsr3_game$' -V
# Optional graphical editor smoke (also enabled in CTest with
# -DLEGACY_TEST_EDITOR_SMOKE=ON):
LEGACY_SMOKE_FSR=1 build/LegacyEngine --smoke-test
```

Distribute the resulting `legacy_fsr3_game` shared library with the editor and
the notices in `data/license`. The adapter isolates the Linux port's global
Vulkan wrappers in a separate shared library; only one adapter context can be
active at a time. The optional build is not supported on Windows. The standalone
SDK feasibility test remains available as `fsr3_engine_experiment`; set
`FSR_DEVICE` to a device-name substring and `FSR_VALIDATION=1` for the port's
self-test when the validation layer is installed.

## Validation and limitations

The adapter test dispatches four engine-rendered moving-triangle frames on
SwiftShader, including a reset and output readback. The synthetic SDK self-test
reported 16.03 dB PSNR for a panning scene versus 13.79 dB for bilinear, with a
flipped-vector control at 13.38 dB. The motion tests cover camera and object
movement, occlusion, mesh replacement, CPU skinning and transactional history.
The 120-frame editor smoke test also ran on SwiftShader, covering embedded and
detached Game, mode changes, a resize, native fallback for an orthographic
camera, and switching off. These tests do not establish real-scene image quality
or production readiness. Hardware GPUs, Vulkan validation layers and real-world
visual behavior still need verification.
