# Debug menu
The top bar's **Debug** menu has two session-only toggles (off by default):
- **Show FPS** displays the editor's rolling frame rate over the Game view. This
  measures the whole editor frame, not an isolated player-render benchmark.
- **Show Colliders in Game** overlays all collider wireframes in green using the
  active Game camera, including during Play. Hidden edges are visible for debugging.

These options do not change the scene or saved project. Without an active camera,
the Game view continues to show only its centered no-camera message.

See [Terminal logs and crash reporting](diagnostics.md) for capturing engine errors and fatal diagnostics.
