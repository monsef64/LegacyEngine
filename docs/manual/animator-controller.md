# Animator controller — manual desktop verification

No automated tests added or run. Native builds/syntax checks do not establish visual runtime correctness.

1. Add Animator, Create controller, drop the three example .leanim assets on the canvas. Rename/move states, choose entry, toggle Loop, create links by right-click and select arrows/All transitions. Save, close/reopen and confirm positions/settings persist.
2. Add Bool/Int/Float parameters. Exercise equality/inequality/numeric thresholds, AND combinations, transition priority up/down, Any State, self transitions and two-way links. Confirm deletion removes state links and parameter deletion is blocked while referenced.
3. Run data/examples/animator on a box (property clips). Observe Idle/Move/Jump, interpolation during fades, nonloop end-pose hold and exit-time gates. Compare hard play vs crossFade (including zero duration). Test pause/resume/stop and re-enable after disabling component/owner. Automatic transitions wait for fades; manual crossFade can interrupt.
4. Repeat with three model clips from the same character, checking skeletal pose blending and loop-to-nonloop transitions. Try a clip from another model: show error rather than corrupting pose/crashing.
5. Script get/setBool/Int/Float, hasState, play, crossFade, stop, pause, resume. Check missing/wrong-type parameters, unknown state, NaN/infinite/out-of-range numbers, queue limits, and destroyed handles. Test parameters set in onStart before the first Animator update.
6. Verify default vs per-instance live parameters on two instances sharing a controller, prefab spawning/duplication, global Animator references and nearest hierarchy lookups. A parameter change on one instance must not affect the other or file defaults.
7. Save/reopen scenes and prefabs with Animator; copy/paste/reset/remove it, undo/redo controller-path/enable changes, and Stop Play rollback. Old removed Animator settings must not instantiate the new schema without a controller key. Animation-only projects remain usable.
8. Open controllers via Inspector, Windows menu and Assets double-click. Test dirty drafts, Save Project/Level/As, New/Discard/Reload, project changes, failed saves, rename/copy controller folders and clip assets. Graph edits must be disabled during Play while live parameter controls still work.
9. Freshly export using the rebuilt player. Confirm controller and referenced clips/models are included/preloaded; compare scripting, fades, looping and level transitions with editor Play. Missing/corrupt/oversized controller JSON and missing clips should produce diagnostics, not crash or silently run an older clip.

Limits: 256 states, 64 parameters, 1024 ordered transitions, 32 conditions per transition, 64 queued script commands; controller JSON max 1 MiB. Layering, blend trees, triggers and AnimatorSheet are outside this implementation.
