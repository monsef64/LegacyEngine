# Screen, Game and File

These namespaces are installed in every LegacyScript. Calls use dots, not colons.
Host I/O failures return `nil, error`; invalid argument types raise script errors.
Use `on("OnFocus", function() ... end)` or a global `function OnFocus() ... end`
for built-in events. These do not replace `onStart` or `onUpdate`.

## Screen

- `Screen.width`, `height`: read-only game render dimensions in pixels, including
  the docked Game viewport; they do not report the editor window size.
- `Screen.currentResolution`: read-only native display mode table with `width`,
  `height`, `refreshRate`. This can differ from the game's viewport dimensions.
- `Screen.getResolutions()`: available display modes, deduplicated by dimensions.
- `Screen.getResolution(width, height)`: checks whether those dimensions are valid.
- `Screen.setResolution(width, height)`: returns success; unavailable modes are
  rejected. Changes window size or fullscreen mode in the player/separate Game
  window. Returns false for the docked viewport (resize its panel instead).
- `Screen.fullScreen`: read/write boolean for player/separate Game windows only.
- `Screen.brightness`: read/write finite 0..1 game-image multiplier; default 1.
  Does not modify OS/monitor brightness or the editor UI.
- `Screen.takeScreenshot("Screenshots/view.png")`: queues a PNG from the next
  rendered game frame, including runtime UI and brightness. The path is relative
  to `Game.userDataPath`; the directory is created. One pending request at a time,
  maximum 8192 pixels on either axis. A successful return means queued, not written;
  encoder/write failures go to the engine log. No game frame means no capture.

`OnResolutionChanged(resolution)` fires after a game-render size change, with
`width`, `height`, `refreshRate` (0 when the refresh rate is not applicable).
`currentResolution` and the event tables are copies.

## Game

Read-only properties: `companyName`, `getDataPath` (a path **property**, not a
function), `isEditor`, `isFocused`, `userDataPath`, `engineVersion`.
The data path is the mounted asset directory; use the user-data path for durable
writes. User data is stored using the OS application-preferences directory,
under LegacyEngine and a sanitized game name. Keep game names stable between
releases to retain saves. Editor Play uses the project folder name; exported games
use the export name. The editor has no export-company context, so companyName is
empty there.

`Game.targetFrameRate = 60` adds a per-frame cap; `-1` removes this cap. Allowed
range is 1..1000 or -1. VSync and the editor's own cap can still reduce FPS.

`Game.openURL("https://example.com")` permits HTTP/HTTPS only. `OnUrlOpened(url)`
means the OS accepted a browser-launch request, not that the page loaded.
`OnFocusLost()` and `OnFocus()` track native game-window focus. Switching editor
panels within the same native window does not constitute focus loss.

`Game.quit()` requests graceful shutdown (stops Play without closing the editor).
A normal player window-close request follows the same path. `OnGamePreQuit()`
is delivered once; it may launch `task(...)` work. Shutdown waits for pending
script tasks and for `Game.blockGameKilling` to become false, with a hard
**ten-second timeout** measured from the request. Task updates continue during
this wait even if the window is unfocused. This cannot intercept forced OS
termination. Editor Stop remains an immediate development/rollback control.

```lua
function OnGamePreQuit()
    task(function()
        wait(0.1)
        Game.saveInt("score", 120)
    end)
end
```

## Game persistence

```lua
Game.saveInt("score", 10)
local score = Game.loadInt("score", 0)
Game.saveVector3("spawn", {x = 1, y = 2, z = 3})
local spawn = Game.loadVector3("spawn", {x = 0, y = 0, z = 0})
```

Paired `saveString/loadString`, `saveInt/loadInt`, `saveFloat/loadFloat`,
`saveBool/loadBool`, `saveTable/loadTable`, `saveVector3/loadVector3` functions use
named keys. The functions now live on `Game`; the old `Saving` namespace and global save/load aliases are removed. Missing keys or a
stored type mismatch return the supplied default (nil if omitted). Corrupted
stores return `nil, error`, not a silently reset save.

Data is persisted immediately in `Saving.json` under `Game.userDataPath`, using
atomic file replacement. Tables preserve integer and string keys and nested
finite numbers, strings, booleans and tables; cycles, functions and userdata are
rejected. Vector3 values are tables with finite x/y/z (bound component vector properties
are also accepted and copied). Limits: keys 128 bytes,
strings 64 KiB, depth 8, 4096 serialized entries/values, store 16 MiB. Saves are
plain JSON, not encrypted. Runtime scene rollback does **not** roll back these
explicit persistent writes.

## File

All paths must be **relative to Game.userDataPath**, not absolute paths copied
from that property. Traversal, virtual packed-member paths, drive prefixes and
symlink escapes are rejected. This is not unrestricted Lua `io` or `os` access.
Do not edit `Saving.json` with File while using the Game persistence API.

```lua
local f, err = File.open("notes/log.txt", "w")
if f then
    File.write(f, "Hello\n")
    File.flush(f)
    File.close(f)
end
for line in File.lines("notes/log.txt") do
    debug.log(line)
end
```

- `open(path, mode)`: opaque handle; r/rb/w/wb/a/ab. Write/append creates parents.
- `read(handle, format)`: `"*l"` (default), `"*a"`, or a nonnegative byte count.
  Line reads return nil at EOF; byte-count reads return an empty string at EOF.
- `write(handle, text)`, `flush(handle)`: success booleans.
- `seek(handle, "set"|"cur"|"end", offset)`: new byte position.
- `close(handle)`: explicitly release; garbage collection also closes handles.
- `lines(handle)`: iterator; caller owns handle. `lines(path)` closes its own
  handle at EOF (and on garbage collection if iteration stops early).
- `rename(oldPath, newPath)`: refuses an existing destination.
- `remove(path)`: files only, never recursive directory deletion.

At most 64 open game file handles; files and individual reads/writes are limited
to 16 MiB. These are synchronous small-data APIs, not a background streaming API.
