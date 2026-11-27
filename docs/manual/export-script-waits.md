# Editor/exported-player wait parity (manual desktop check)

No automated tests were added or run for this change.

1. Attach a script with `onStart`, `onUpdate`, and an independent `task` that logs before and after `waitForSeconds(2)`. Keep `onUpdate` logging throttled. Verify other callbacks continue while the task sleeps.
2. Play in the editor, then freshly export with the rebuilt native LegacyPlayer template and compare the same scene and settings. Do not use an old player template: it contains the old scheduler.
3. Compare at 30, 60, and uncapped FPS. A two-second wait should take approximately two playing seconds, rounded up to the next update. Chained waits still round individually to a frame; they are not a sample-accurate repeating timer.
4. On a heavy scene that regularly takes more than 250 ms to render, verify waits still count elapsed playing time rather than stretching according to the movement-delta cap. Movement keeps its safety cap.
5. Check a long-duration wait at high FPS; its double-precision countdown must keep advancing. Check zero-duration waits resume on the next update without freezing the engine.
6. In editor Play, pause/resume; disable/re-enable the script owner; then Stop/restart. Waits should freeze while paused/inactive, resume without rerunning onStart, and cancel on Stop.
7. In the exported game, unfocus/refocus the window. Gameplay waits remain paused while the player is suspended.
8. Transition to a slow-loading level whose onStart waits before changing a visible object. The new wait must not be consumed by the old level's frame or blocking loading work. Compare with editor Play.
9. Check wait(audio.onAudioFinished), animation event waits, wait inside onFixedUpdate, and task cancellation on object destruction. Event waits remain sequence-based, separate from seconds.

Implementation: movement callbacks retain capped deltaTime. LuaConsole/ScriptComponents forward a separate elapsed-playing-time delta to LuaScene's double-precision wait countdown in both editor and exported player. Invalid/nonfinite deltas cannot poison countdowns; wait arguments remain finite and limited to 0–86400 seconds.

Desktop verification is still required; successful native builds do not demonstrate timing parity on the user's scene.
