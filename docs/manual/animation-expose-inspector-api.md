# Manual verification: animation, exposed fields and Inspector API changes

No automated tests were added or run. Verify desktop OpenGL behavior on Linux and
Windows MinGW-w64. Compilation is not a high-poly performance benchmark.

## Animation loading/performance

- Play a large multi-part skinned model using play(), playClip(AnimationClip(path)),
  and exposed AnimationClip fields. While a new clip loads, editor navigation and
  the current animation should remain responsive. Inspect frame-time spikes on the
  first Play, repeat Play, first fade, repeat fade, and an interrupted crossfade.
- Compare with the previous engine using the same model, camera, shadows and FPS
  cap. Include multiple animated entities and a high-poly model with many bones.
  GPU buffer allocation/upload can still have a first-use cost; do not infer a
  measured speedup from a successful build.
- Stop or Reset while a load is pending. It must not unexpectedly start later.
  Confirm callbacks, loop completion, same-model crossfades, incompatible fade
  errors, zero-duration fades, pause/resume, and property-track clips still work.
- Switch projects/levels while workers run, delete an animated owner, edit/reimport
  model assets, and Stop editor Play. No stale results or runtime edits should leak.

## expose declarations and prefab assets

- Use expose int score=10, expose float speed = 4.5, expose bool ready = true,
  expose string title = "Player", expose Prefab enemy = "Prefabs/Enemy.prefab",
  and expose AnimationClip walk = "Animations/Walk.leanim". Integers must reach Lua
  as integers. Check highlighting, completion and diagnostics in engine/VS Code.
- Declarations inside quoted strings/block comments or mid-line must not become
  fields. Global declarations must continue loading with saved values.
- Drop/select a prefab into its exposed asset field and spawn it. Scene-object
  reference fields remain scene references. Save/reload and duplicate scripts;
  defaults/overrides, undo and Play/Stop rollback must remain correct.

## Inspector, shadows and skyboxes

- Asset fields show filename.extension; hover shows the complete reference. Test
  same-named files in different folders, packed members, clearing, copy/paste and
  drops. No saved paths should be shortened or changed merely by drawing the UI.
- Toggle Enabled on expanded and collapsed headers for lights, Skybox, Camera,
  Decal, UI, effects and primary scripts. Additional scripts remain independent.
  Confirm existing runtime behavior, Camera exclusivity, reset/undo/save/reload.
  Transform/Collider must not gain unsupported enabled behavior.
- Disable MeshRenderer.castShadow: the mesh stays visible and receives lighting,
  but stops casting realtime shadows. Check animated/model parts, multi-edit,
  prefabs and script changes. Rebake static lighting to verify baked cast shadows;
  terrain and other casters must remain unaffected. Older scenes default true.
- HDR skyboxes use RGB9_E5 by default with no compression checkbox/script toggle.
  Open old scenes with the flag false, fade between HDR skyboxes and check extreme
  highlights. Source HDR bytes must remain unchanged.

## Game persistence and randomness

- Migrate Saving.save/load calls to Game.save/load for String, Int, Float, Bool,
  Table and Vector3. Verify the existing Saving.json still loads across restarts.
  The Saving namespace and old global save/load aliases must no longer exist.
- Call Random.randomFloat, randomInt, randomBool and randomString,
  plus their global aliases. Verify equal bounds, inclusive integer endpoints,
  both boolean outcomes, zero-length strings and 4096-character strings.
- Reject reversed/nonfinite ranges, noninteger integer bounds, negative/oversized
  string lengths without crashing. Random strings are alphanumeric, not a crypto API.
- Export and run a clean player with exposed prefab/clip assets; the dependency
  collector must still include them and persistent writes must survive scene rollback.
