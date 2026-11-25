# Navigation, script attachment, and loading feedback — manual checks

No automated tests added or run.

- Bake a flat floor and ramps. Give a centered two-unit-high model a NavMeshAgent with Height offset 1. Navigate over both and confirm the bottom stays above the surface without slowing or oscillating. Try offset 0 for a foot-pivot model. Confirm height does not change destination/path distances.
- Save/reopen the level, duplicate/copy the component, use a prefab, and undo/redo Height offset edits. Old levels with no heightOffset must default to 0. Test both editor Play and a freshly exported player. Stop Play must restore the original Transform/settings.
- In script, set speed/heightOffset directly and via setSpeed/setHeightOffset. stop must immediately clear velocity and retain the route; start must resume it. resetPath must discard it. Try invalid/nonfinite/out-of-range values and destroyed component handles; other scripts must keep running.
- Drop a .lua/.ls asset on unused Inspector space with a selected GameObject. It should create LegacyScript and populate global fields. Drop a second script to append it, repeat the same drop (no duplicate), then undo/redo and save/reopen. Drop non-script assets and verify no script attachment. Existing reference fields must still accept their intended assets without also appending a script.
- Compare Play preloading in docked Game and separate Game window. Only centered Loading.. text should appear; the editor outside Game must remain visually unchanged. Test with Game initially showing no active camera. Confirm editor ImGui remains functional after loading and after errors/cancellation.
- Export and run a heavy level, then request an async level transition. Confirm centered Loading.. text appears inside Game during staging, disappears on completion/failure, and no old full-window progress screen remains. Close during startup loading and verify cancellation.

Desktop visual/runtime verification remains required.
