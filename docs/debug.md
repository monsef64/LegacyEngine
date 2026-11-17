# Debug menu
The top bar's **Debug** menu has two session-only toggles (off by default):
- **Show FPS** displays the editor's rolling frame rate over the Game view. This
  measures the whole editor frame, not an isolated player-render benchmark.
- **Show Game Colliders** overlays all collider wireframes in green using the
  active Game camera, including during Play. Hidden edges are visible for debugging.

These options do not change the scene or saved project. Without an active camera,
the Game view continues to show only its centered no-camera message.

See [Terminal logs and crash reporting](diagnostics.md) for capturing engine errors and fatal diagnostics.

## Profiler

Open **Debug → Profiler** to record CPU frame timings. It shows a rolling graph
and the last, average, peak and average-frame percentage over up to **120 frames**.
Use **Pause sampling** to inspect results without replacing them.

The measured sections are events/input, editor panels/assets, script updates,
hierarchy, physics (including fixed-update scripts), camera updates, audio/pose
sync, Scene rendering, Game rendering, ImGui submission, presentation/vsync wait,
and other frame work. Renderer rows break out light/shadow setup, skyboxes and
surfaces/gizmos; these are **nested** inside Scene/Game timings, not additional
frame costs. The timings include synchronous driver work but **do not measure GPU
execution time**, the audio device callback, or individual functions/scripts.
Vsync can dominate the Present row. Asset decoding performed during a frame is
included in the calling section. Sampling is disabled while the profiler is
closed; histories are bounded, session-only, and do not dirty the project.

## VSync and rendering cost

**Project > Project Settings > Rendering > Enable VSync** changes the OpenGL
swap interval and persists it in the project's `Settings/Rendering.json`.
New projects default to VSync on. Driver overrides can still affect presentation;
the panel shows the interval reported by SDL and reports failures rather than
silently treating an unsupported request as successful.

Disable VSync when comparing uncapped performance, then use **Debug > Profiler**
to compare Scene, Game, light/shadow setup and presentation wait. Both visible
viewports render independently. CPU timings do not measure GPU execution time.
Frustum planes are now normalized once per view/shadow face rather than once per
object test. This reduces CPU culling cost without changing shadow quality, but
is not evidence of a particular FPS gain on your GPU.
