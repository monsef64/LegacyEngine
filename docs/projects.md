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
