# Exporting a standalone game

Use **File > Export Game** while not playing. Build/install `LegacyPlayer` beside
`LegacyEngine`; the editor executable is not a player template. Release packages
include both. Export on **Windows (MinGW-w64)** for Windows, or on **Linux** for
Linux. The export window does not cross-compile or download a player.

1. Save your script and asset edits. Choose **Current Scene** to snapshot the
   current editor scene, or select a registered starting level.
2. Enter Game Name and Version (`major.minor.patch`, each 0–65535).
   Company, Description and Copyright are optional. Game Name is also the executable basename.
3. Assign an icon using **Browse** (or type an absolute path): multi-resolution **ICO** on Windows, **PNG** on
   Linux (at most 4 MiB).
4. Choose a **new, nonexistent output folder** and the native `LegacyPlayer`
   template. Output Browse selects a parent directory and appends Game Name, so it
   does not require creating the output first. Browse beside Native player chooses
   the player executable. Choose a chunk size from **1–64 MiB** (default 16). Already exported executables cannot be reused as templates.
5. Click **Export Game** and wait for success or an actionable error. Do not edit
   project files externally while packaging. Existing output is never overwritten.

Windows produces `GameName.exe`, `GameName_Data/` and runtime DLLs beside the EXE.
The executable receives icon, company, description, copyright and version
resources. Linux produces the executable `GameName` and `GameName_Data/`; its PNG
is used as the runtime window icon, not as embedded Windows-style resources.
Distribute the **whole output folder**. Linux still requires compatible system
libraries/graphics drivers; this is not an AppImage or a universal static binary.
The standalone runtime has no editor windows, scene-saving tools or project browser.
F11 toggles fullscreen.

## Packaging and encryption limitations

Project assets/settings, the chosen scene and engine runtime data are packed into
**user-sized Zstandard-compressed, authenticated encrypted chunks**, with an encrypted manifest and a fresh
per-export key/identifier. The player authenticates package contents before use.
Plaintext license notices are also supplied as `GameName_Data/LICENSES.txt`; keep
them in your distribution. Current package limits include 2 GiB total plaintext.

This protects against casual browsing and accidental modification, **not determined
extraction or DRM bypass**: the executable necessarily contains the decryption key.
The player extracts authenticated runtime files into a private temporary directory
because existing loaders require filesystem paths. Normal shutdown removes it;
a crash or forced termination may leave plaintext files behind. Do not package
secrets, credentials or private data. Encryption is not an access-control boundary.

Build both native binaries with:

```sh
cmake --build build --target LegacyEngine LegacyPlayer --parallel 2
```

CMake marks built player templates after linking. When stripping a distribution
copy, reapply the marker **after** stripping, as the release workflows do:

```sh
cmake -DPLAYER=/path/to/LegacyPlayer -P cmake/player_template.cmake
```

Do not strip or modify an already exported executable: its package key/footer is
part of the export. See the [manual checklist](../docs/manual/export-scripts-previews.md).


Export folders must be outside the source Assets tree; `Project/Builds/Game` is a
suitable destination. A different Windows drive is also supported. Existing output
is never overwritten. Current player templates are required for adjustable chunks;
older exported games retain their legacy 16 MiB package compatibility.

The export window contains controls and operation results only; platform constraints,
package layout, security limitations and build instructions are documented here.

## Runtime data lookup

Standalone players resolve shaders and built-in particle textures from their mounted
`Engine/data` directory rather than beside the executable. This avoids the renderer
initialization failure caused by looking for loose `data/shaders` in an encrypted
export. Missing/invalid graphics drivers or unsupported OpenGL contexts can still
cause renderer initialization errors; inspect the player log for the failing stage.

## Scene, level and prefab dependencies are packaged

Exports collect a dependency closure from the exported scene and every level in
`Assets/Levels/levels.json`. Every project `.prefab` is also an export root, so
runtime `spawn("Prefabs/" .. name .. ".prefab", position, rotation)` works even
without a literal full prefab path in a scene or script. Each prefab's models,
materials, child assets, scripts and their discoverable dependencies are included
recursively. This can increase package size when a project keeps unused prefabs;
move development-only prefab files outside Assets if they must not ship.
Referenced materials, model containers, animations, prefabs, terrain layers,
foliage, images, audio, lightmaps and script dependencies are followed recursively.
Other unreferenced files and unregistered, unreferenced levels are left out. Packed model
members travel with their model container; exports do not rewrite those containers
or remove individual embedded members. Engine runtime resources and project
Settings are retained separately from this Assets filtering.

Literal paths in included scripts and UI are detected, including relative UI/CSS
paths. Apart from the prefab roots above, an exporter cannot infer an arbitrary
asset path constructed at runtime, such as
`"Textures/" .. name .. ".png"`. Declare these dependencies explicitly in
`Settings/ExportAssets.json` before exporting:

```json
{
  "include": ["Textures/RuntimeChoices/"]
}
```

Entries are Assets-relative files or directories. Included directories retain all
files inside, and each included asset's own dependencies are followed. Missing
explicit entries stop export rather than silently making an incomplete package.
Keep this list specific instead of including all of Assets. Test every level,
spawn path and UI screen in the exported player, especially when paths are built
from save data, downloaded configuration or script variables. Referenced symlinks
are rejected; dependency cycles are handled without packaging duplicate files.

## Remembered export information

The Export Game window stores its fields per project in `Settings/Export.json`:
game name, company, version, description, copyright, icon, output folder,
start-scene selection, chunk size and Debug Mode. Edits save automatically
(after a short typing delay or when editing ends), including when the window is
closed. You do not need to export successfully just to retain this information.
Project Save/Save As also carries the export fields to the destination project.
Opening another project loads its own settings rather than retaining the previous
project's metadata.

Each time Export Game opens, **Native player template** is reset to
`LegacyPlayer` (`LegacyPlayer.exe` on Windows) beside the running editor. Old saved
player paths are ignored and new ones are not persisted. You can browse for a
different template for the current opening (for example, another target OS), but
reopening the window or switching projects restores the local template.
Icon/output paths remain machine-local: check them when moving a project between
machines or operating systems. Existing output folders still cannot be
overwritten; choose a fresh destination for another export. `Current Scene` stays
a relative selection, not a frozen scene name. A file chooser opened for an old
project cannot overwrite the newly opened project's fields when its result arrives.

`Export.json` is editor-only and is excluded from the exported runtime package so
local workstation paths are not shipped. Runtime settings and the explicit
`Settings/ExportAssets.json` dependency list keep their existing behavior.

## Compression policy

Editor saves/imports keep mesh/model/terrain assets as uncompressed compact JSON;
older Zstandard/zlib assets remain readable and are rewritten uncompressed only
when saved/reimported. Export does not modify source assets. Exported package
chunks use Zstandard level 3 before authenticated encryption, including assets,
settings and engine data. Chunk size and the 2 GiB limit describe **expanded**
data; disk size depends on content (already-compressed images may not shrink).
The LegacyGame2 manifest records the codec. The updated player still reads old
LegacyGame1 packages, but new exports require an updated native player template.
Compressed frame size/content and expanded chunk lengths are bounded and checked.

Project loading also avoids an intermediate JSON serialization/reparse and transfers
fully staged ECS pools instead of cloning them twice. These remove redundant work;
no fixed load-time speedup is promised without measuring a representative project.

## Debug Mode and exported console

**File > Export Game > Debug Mode** is off by default and is saved per project
in `Settings/Export.json`. Enable it to include a read-only runtime log console
in the exported game. Use an updated native player template when exporting.

Press the backtick/grave key (the physical key below Escape on a US keyboard) to
show or hide it; Escape or its close button also closes it. The game keeps
simulating while the console is visible, but gameplay input is suppressed and
the cursor is released. It shows SDL/engine and script log output, with Clear
and Auto-scroll controls. Log retention is bounded to 512 messages, each capped
at 8192 bytes; this is not a persistent log file or a crash reporter.

The console cannot execute Lua or commands and does not restore the removed
`GUI` scripting namespace or `onGUI` callback. Exports with Debug Mode disabled,
and old packages without the flag, cannot open it. Build normal releases with
Debug Mode off so internal script/log messages are not exposed through this UI.

Prefab packaging changes only affect **new exports made with the updated editor**.
Rebuild/install the player template with the editor, then export to a fresh output
folder. Updating the editor/player alone does not add files to an existing game
package. If spawning still fails, enable Debug Mode, open the player's backtick
console, and copy the error containing the prefab path. Spawn paths are relative
to Assets (`Prefabs/Crate.prefab`, not `Assets/Prefabs/Crate.prefab`).
