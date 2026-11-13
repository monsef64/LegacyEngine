# Audio and input
```lua
local audio
function onStart()
    audio = createAudioSource(self, AudioClip("music.mp3"))
    audio:setVolume(0.7)
    audio:spatialBlend(1)  -- 0 = 2D, 1 = 3D
    audio:setDistance(50)  -- maximum audible 3D range
    audio:fadeIn(2)
end
```

AudioSources show a wireframe speaker glyph only in the Scene editor. Selecting a
spatial source also shows its maximum hearing range as three wire circles in
world units (independent of Transform scale). These gizmos never appear in Game.

Use `self:getComponent(AudioListener):pause()` to disable reception and
`:start()` to enable it again. Sources continue their playback timelines while
the listener is paused; this does not pause or restart individual clips.

Add an enabled AudioListener to the camera/player: gameplay audio is silent without
one, even in 2D. 3D audio uses source/listener world positions and listener rotation;
gain falls linearly to zero at Max Distance. Multiple listeners use the enabled one
with the lowest entity ID. Fade-in continues to the clip's end; fade-out stops after
the fade. WAV/MP3 clips are limited to 32 MiB files / 128 MiB decoded, with a 256 MiB
cache and 128 voices. OGG and automatic Doppler/occlusion are not supported.

Input functions include raw `isKeyPressed/Down/Released`, mouse button equivalents,
mouse position/delta/wheel, gamepad button pressed/down, gamepad axes/sticks, and
named `isButtonPressed/Down/Released`. Digital helpers take defined button names:
`getInputAxis(negative, positive)` and
`getInputVector(left, right, down, up)` (normalized diagonals). Named actions accept up to three alternative keyboard, mouse or gamepad-button
bindings. Existing v1 keyboard settings are read automatically; saves use v2. Edges/deltas last one render
frame. Gameplay input is gated by focus/Play state and disabled while editing text.
The first available gamepad is used, with a 0.15 dead zone. Mapping slots are
buttons, not analog axes; raw stick/axis APIs remain available.

Mouse control functions return a success boolean:
```lua
lockCursor()       -- relative motion; cursor hidden while locked
unlockCursor()     -- leave relative mode
hideCursor()       -- hide without locking
showCursor()       -- request visible cursor (relative mode still hides it)
local visible = isCursorVisible()
local locked = isCursorLocked()
```
Queries report native SDL cursor/window state. Pause, Stop, focus loss and normal
shutdown release the script cursor so the editor does not trap it. Resume does
not automatically relock; scripts may lock again when needed.

See [the API/type definitions](../scripts/definitions/legacyengine.lua) and
[the completion catalog](../src/scripting/lua_definitions.hpp) for more signatures.

Audio decoding and device playback use vendored miniaudio 0.11.23. Scene audio
retains a deterministic main-thread mixer; the device callback consumes bounded
stereo float PCM without accessing GameObjects. Asset previews have their own
playback queue and do not require an AudioListener. WAV and MP3 decoding are
provided by miniaudio; there is no separate dr_libs dependency.
