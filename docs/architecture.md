# Source organization
| Directory | Responsibility |
| --- | --- |
| `src/app/` | Application entry point and editor/runtime orchestration |
| `src/core/` | Math, ECS, engine components and transform hierarchy |
| `src/physics/` | ReactPhysics3D integration and collider debug drawing |
| `src/rendering/` | OpenGL renderer, procedural meshes and viewport surfaces |
| `src/scene/` | Editor camera and scene-camera calculations |
| `src/audio/` | WAV/MP3 decoding, mixing, listener/spatial audio and previews |
| `src/input/` | Shared keyboard, mouse and gamepad snapshots |
| `src/scripting/` | Lua sandbox, object/component bindings, input bindings, exports and lifecycle |
| `src/editor/` | Panels, Inspector, gizmos, settings, theme, history and playback controls |
| `src/assets/` | Asset resolution, import, storage and copying |
| `src/levels/` | Project-owned level registry, IO and deferred transitions |
| `src/project/` | JSON/legacy project formats, settings helpers and project storage |
| `src/platform/` | Native file dialogs and system Trash integration |
| `tests/` | Engine/editor regression tests and generated audio fixtures |
| `extensions/legacyscript/` | VS Code extension and bundled language server |
| `scripts/definitions/` | Editor-only Lua API/type reference |
| `third_party/` | Vendored dependencies and licenses |

Includes use subsystem paths such as `physics/physics.hpp`, rooted at `src/`.

## Formatting and ownership boundaries

First-party engine C++ and GLSL share the repository `.clang-format` policy:
four-space indentation, explicit branch/loop bodies, and a 100-column target.
Generated dependencies and vendored libraries are excluded. The pinned formatter
and CI check prevent new features from reintroducing compressed, multi-statement
lines:

```sh
python3 -m pip install clang-format==21.1.8
python3 tools/format_code.py          # apply
python3 tools/format_code.py --check  # read-only check, also run in Actions
```

`legacy_levels` owns level-file IO, registry validation and deferred transitions.
Its implementation is compiled once in `src/levels/level_system.cpp`, rather than
being repeated in every consumer of the public header. It links JSON and threading
support, not Lua or ImGui. The scripting adapter depends on this service, not the
other way around. File formats and frame-boundary transition behavior are unchanged.

`src/core/frustum.hpp` contains reusable view-frustum math, independent of lights
and OpenGL. Rendering and light collection normalize planes once per view rather
than per tested sphere.

## Hot-path traversal and scratch storage

`World::each()` retains its mutation-safe snapshot semantics for scripting and
other callbacks that may add/remove components or entities. Read-only systems can
use `World::eachReadOnly()` to traverse existing pools without an ID snapshot or
pool creation. It passes const component references and caches pool lookups.
**Do not structurally mutate the World through another alias during this traversal.**
This is an explicit iterator contract, not a thread-safety guarantee.

The renderer uses read-only traversal and owns reusable light, shadow-caster and
transparent-draw buffers. Capacity survives across frames and Scene/Game views;
entries are rebuilt for every view, so transform/light/material edits stay live.
Transparent packets borrow MeshRenderer references only during drawing, avoiding
copies of their asset-path strings. Their explicit insertion-order tie-break keeps
equal-depth ordering deterministic without `stable_sort`'s temporary buffer.
Meshes are frustum-culled before resolving materials. Shadow casters are still
collected independently: offscreen objects can cast visible shadows.

These are targeted CPU/allocation changes, not a rewrite of the renderer or a
claim about GPU execution time or overall FPS. Scratch capacity is retained at
its high-water mark until the renderer is destroyed.

## Reproduce the CPU allocation benchmark

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLEGACY_BUILD_BENCHMARKS=ON
cmake --build build --target render_setup_benchmark
./build/render_setup_benchmark
```

The optional executable measures 5,000 collections of 256 visible lights using
the current convenience API versus warmed caller-owned scratch. It checks the
result count and fails if the warmed collection allocates. In one sandbox run,
the convenience path made 45,000 allocations and took 44.5 ms; reused scratch
made zero allocations and took 40.3 ms. This comparison isolates the current
collection APIs—not the entire old/new engine. Timings vary; it is not a GPU or
end-to-end frame benchmark. The target is off by default and is not packaged.
