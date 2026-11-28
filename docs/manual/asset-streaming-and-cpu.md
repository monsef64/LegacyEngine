# Scene loading and CPU processing

## Implemented scope

The startup project launcher shows **New Project**, **Open Project**, and up to
12 recent projects. No editor dockspace, Hierarchy, Inspector or Scene view is
submitted until a project opens successfully. Buttons use embedded Font Awesome
icons, so no external launcher image files are required. Missing recent paths
remain visible with a tooltip; Open Project lets you locate a moved file.
Canceling the picker returns to the launcher; closing the launcher exits.

New Project uses the built-in ImGui save picker. Choose a new `.legacy` file in
its own folder (the picker can create folders). Existing project files, populated
Assets directories and existing Settings directories are not overwritten by New
Project. The new project starts with the default cube and directional light.

Opening a project stages its saved current level (or first registered level when
no current level is selected) before publication. Projects without registered
levels use the scene in the project file. Prefabs, terrain data, dependency
models/textures, animation clips and audio are prepared before the editor is
revealed. A Loading screen is presented during preparation; a private warm-up
render initializes first-use scene resources without presenting a partial Scene.
Failures return to the launcher or retain the previous editor world.

Referenced scene models and first-use material/foliage/particle/lightmap textures
now wait for decoding/upload rather than displaying temporary missing geometry
or untextured placeholders. Skybox decoding also completes before its first
use. This deliberately trades shorter background-loading frames for predictable
first-use readiness: changing an asset or assigning a new large model can pause
the current frame. Invalid/missing resources still report errors/use their
existing failure fallback; they cannot be made ready by waiting.

CPU workers remain an implementation detail for parallel preparation, previews
and resident texture hot-reload. This is not lazy reveal of the opened scene.
The pool has 1–4 workers and a bounded 32-job queue. Open/Play preloading pumps
bounded GPU uploads while keeping the loading screen responsive. GPU operations
remain on the render thread. Futures own CPU snapshots and cannot publish into
destroyed renderers or ECS worlds.

Resident textures retain the 128 MiB / 128-entry cache budget. Scene preload
protects required scaled textures against path-order eviction and reports an
oversized working set instead of silently loading forever. This does not preload
every unrelated asset in the browser or guarantee all possible dynamic script
paths are known. Preview thumbnails remain generated/cached on demand.

Matrix multiplication uses SSE2 on supported x86 builds, NEON on ARM64, and a
scalar fallback elsewhere. Jolt simulation uses its own bounded worker pool;
contact-map writes and ECS publication remain synchronized.

## Manual verification (not executed in the sandbox)

1. Launch normally: only the project launcher appears, with working icons,
   gradient header and recent-project cards. Resize to narrow/high-DPI layouts;
   confirm scrolling, clipped long paths/tooltips and readable error messages.
2. Cancel New/Open, reopen the picker, create a folder and a new project. Confirm
   saved Assets/Settings belong to that project. New Project must not overwrite
   another project's file or populated Assets/Settings directories.
3. Open recent and moved/deleted recent paths. Corrupt a project or its current
   level: an actionable error should remain in the launcher, not an empty editor.
4. Open a large textured/animated scene. The Loading screen must precede the
   editor, with no gradual object/texture appearance after reveal. Verify the
   saved current level, editor camera, prefab overrides, terrain and lightmaps.
5. Open another project through File/Open and Open Recent. Verify unsaved-change
   prompts, rollback on failure, undo history reset, root isolation and shutdown.
6. Assign a new model, texture, grass texture, particle sprite or skybox after
   opening; wait for its first-use work, without a temporary missing placeholder.
   Exercise resident hot-reload and repair missing/corrupt assets.
7. Exercise animated glTF instances, level switches, Play/Stop, character movement,
   contacts/triggers and scene queries. Compare load time, frame spikes and peak
   memory on the same hardware; no performance improvement is guaranteed.
8. Build/check Windows MinGW-w64 and ARM64 separately. No automated tests were
   added or run for this change; the smoke-test mode still bypasses the launcher
   for its existing standalone rendering fixture.
