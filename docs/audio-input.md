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
source shows a light-blue latitude/longitude wire sphere whose radius follows
Max Distance immediately, in world units independent of Transform scale. The
range gizmo is shown for both 2D and 3D sources, although only spatial audio uses
the distance for attenuation. These gizmos never appear in Game.

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

## GameObject mouse callbacks

Attach a script and a Collider to the object. Make sure an active Game camera
can see it, press Play, and point inside the Game image or separate Game window.
The nearest active Collider under the pointer receives the callbacks; a rendered
mesh without a Collider is not pickable. Camera viewport bounds are respected.
When the cursor is locked, picking uses the center of the Game view.

| Callback | Trigger |
| --- | --- |
| `onMouseEnter()` | Hover changes to this object |
| `onMouseExit()` | Hover leaves this active object |
| `onMouseOver()` | Each playing frame while hovered |
| `onMouseDown()` | Left-button press over the object |
| `onMouseUp()` | Release corresponding to its Down, even if released elsewhere |

```lua
function onMouseEnter()
    debug.log("Hover", self:getName())
end

function onMouseDown()
    debug.log("Clicked", self:getName())
end

function onMouseUp()
    debug.log("Released", self:getName())
end
```

These are targeted object callbacks, not global mouse-button polling. Define them
in the target object's attached script, or assign an object callback such as
`self.onMouseOver = function() ... end`. Inactive/destroyed objects receive no
mouse callbacks. If nothing fires, check Collider, camera, object/parent activity,
Play state, and whether another Collider is closer to the camera.

## AudioSource callbacks and event waits

Define these global callbacks in a script attached to the **AudioSource owner**:

| Callback | Trigger |
| --- | --- |
| `onAudioStarted()` | Primary clip starts or restarts through play/fade-in |
| `onAudioFinished()` | Non-looping primary clip reaches its natural end |
| `onAudioPaused()` | Playing changes to Paused |
| `onAudioResumed()` | Paused changes to Playing without restarting |
| `onAudioStopped()` | Explicit stop, fade-out stop, or playback failure |

Repeated pause/resume calls that do not change state do not produce another event.
One-shot playback is separate and does not produce primary-clip callbacks. Looping
clips do not naturally finish. AudioListener pause/mute changes audibility, not
individual source playback state; source callbacks are not listener events.

```lua
function onStart()
    local audio = createAudioSource(self, AudioClip("Audio/example.wav"))
    waitForSeconds(5)
    audio:play()
    wait(audio.onAudioFinished)
    debug.log("Continue after the primary clip finishes")
end

function onAudioFinished()
    debug.log("Finished callback", self:getName())
end
```

Import `Audio/example.wav` before running this example and add an enabled
AudioListener to an active object to hear playback. The `wait` argument is an
opaque token: use `audio.onAudioFinished`, **not** `audio.onAudioFinished()` and
not the string `"onAudioFinished"`. The same token pattern works with Started,
Paused, Resumed and Stopped; waits observe the next event, never a past one.

A wait suspends only its current lifecycle/object callback. Other callbacks and
the engine keep running. Do not wait at script top-level, in custom/level event
listeners, or in onDestroy. Play pause and object inactivity freeze timer waits;
Stop/reset cancels them. Removing the AudioSource cancels the waiting continuation.

If a wait never resumes, check for a looping clip, a paused source, an inactive
owner, or a stop/fade-out instead of natural completion. Decode failure reports
diagnostics and Stopped rather than Finished. See the
[callback and wait quick guide](scripting-reference.md#callback-and-wait-quick-guide)
and [activity rules](scripting.md#object-activity-mouseaudio-callbacks-and-waits).
