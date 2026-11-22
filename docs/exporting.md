# Exporting a standalone game

Use **File > Export Game** while not playing. Build/install `LegacyPlayer` beside
`LegacyEngine`; the editor executable is not a player template. Release packages
include both. Export on **Windows (MinGW-w64)** for Windows, or on **Linux** for
Linux. The export window does not cross-compile or download a player.

1. Save your script and asset edits. Choose **Current Scene** to snapshot the
   current editor scene, or select a registered starting level.
2. Enter Game Name, Company, Version (`major.minor.patch`, each 0–65535),
   Description and Copyright. Game Name is also the executable basename.
3. Assign an absolute icon path: multi-resolution **ICO** on Windows, **PNG** on
   Linux (at most 4 MiB).
4. Choose a **new, nonexistent output folder** and the native `LegacyPlayer`
   template. Already exported executables cannot be reused as templates.
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
**16 MiB authenticated encrypted chunks**, with an encrypted manifest and a fresh
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
