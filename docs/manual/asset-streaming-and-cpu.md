# Background asset loading and CPU processing

## Implemented scope

- Referenced scene meshes load and decompress on demand on a shared CPU worker
  pool. Material textures read and decode in that pool when first requested.
  Existing zstd/zlib model compatibility is retained.
- The pool has 1–4 workers and a bounded 32-job queue. Each renderer/cache admits
  at most two mesh/texture jobs at once. Uploads stay on the render thread, with
  at most one completed mesh per draw and one texture per cache beginFrame.
- Missing assets are skipped or use existing fallback rendering until ready.
  Cache/root changes discard old futures; jobs own their inputs and cannot
  publish into destroyed renderers or ECS worlds.
- Matrix multiplication uses SSE2 on supported x86 builds, NEON on ARM64, and a
  scalar fallback elsewhere. No AVX or fast-math requirement was introduced.
- Jolt simulation uses its own bounded worker pool. Contact-map writes are
  synchronized; ECS changes and event publication follow the simulation barrier.

This is demand loading of referenced assets, not visibility-cell streaming,
GPU mip streaming, or mesh eviction. Resident textures retain the existing
128 MiB / 128-entry LRU budget. Explicit editor bounds, model metadata,
import, collider creation and animation metadata paths can still load
synchronously. GPU uploads and animation skinning can still consume main-thread
time. Performance improvements have not been benchmarked.

## Manual verification (no automated tests run)

1. Open a scene containing several large baked models and material textures.
   Confirm initially missing geometry/textures appear without reopening the scene.
2. Inspect model, animation, material and texture previews while assets load;
   confirm cached images refresh after loading finishes.
3. Switch projects and close previews while jobs are pending. Confirm no stale
   asset from the old project appears, no crash, and shutdown completes.
4. Try missing/corrupt assets, then repair them. Confirm retry/fallback behavior
   and that other assets continue loading.
5. Exercise animated glTF instances: verify independent poses and unchanged
   static geometry while background mesh uploads complete.
6. Exercise many colliding bodies, sensors, character movement and Lua contact
   events. Verify enter/stay/exit behavior and safe scene reload during play.
7. Compare frame times before/after on the same scene and hardware, including
   loading spikes and peak memory. Do not infer a guaranteed FPS improvement.
8. Build and manually check Windows MinGW-w64 and ARM64 separately; these
   platform paths have not been validated in this Linux workspace.
