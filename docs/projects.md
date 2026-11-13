# Projects
Use one project per directory:

```text
MyProject/
  MyProject.legacy                 # structured JSON, format 9
  Assets/                         # scripts, audio and other imported assets
  Settings/
    ProjectSettings.json          # project/editor-camera settings
    InputSettings.json            # three-slot named button mappings (v2)
  .cache/thumbnails/               # disposable texture previews; not source assets
```

Save As copies assets/settings to the destination, refusing conflicting asset
content. Untitled data lives in an isolated temporary project until saved.
Distribute the entire project directory, not just the `.legacy` file. Projects
in the same directory share Assets and Settings. Legacy text formats and old
asset-directory references remain readable; saving writes the current layout
without deleting old project data. Scene limits: 10,000 objects, 16 MiB project
JSON and 64 KiB editor source. Runtime state, logs and history are not serialized.

## Levels

Use **Project > Levels > New Level...** to create an empty level. The current
level is saved first (initially as `Main`). **Save Current Level** saves the active
world and editor camera; choose a name in the same menu to switch.
Levels belong to the project: `Assets/Levels/<name>.level` contains the scene JSON,
and `Assets/Levels/levels.json` records creation order and the current level.
Project Save/Save As carries these assets with the project. Existing single-scene
projects remain readable; the first level save creates their Main level.

LegacyScript globals:

| Function | Behavior |
| --- | --- |
| `loadLevel(name)` | Request a switch to a registered level |
| `loadLevelAsync(name)` | Read on a worker thread, validate/apply on the main thread |
| `unloadLevel(name)` | Unload the current level; optional name must match |
| `restartLevel()` | Reload the current level's saved version |
| `getCurrentLevel()` | Active level name, or `nil` after unloading |
| `setCurrentLevel(name)` | Same transition as `loadLevel(name)` |
| `nextLevel()` / `previousLevel()` | Switch in creation order, without wrapping |

Transition functions return `(accepted, error)`. Acceptance is not completion;
`getCurrentLevel()` changes only after a successful frame-boundary transition.
Omitting a load name reloads the current level, or the first registered level if
none is active. Only one request can be pending. Async decoding errors appear in
the Console and leave the current world unchanged. A transition stops the old
scripts/audio and starts the destination scripts/audio during Play. Stop cancels
pending loads and restores the editor world, console source, camera, selection
and undo history. Runtime switches never overwrite saved levels or the editor's
persisted current-level choice. Create/save/switch through the editor menu is
available only outside Play; level names cannot contain filesystem separators.

Right-click the Hierarchy root and choose **Rename Level...** to rename the active
level. This updates its `.level` filename and registry entry; duplicate or invalid
names are rejected without changing the current level.
