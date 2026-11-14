# Importing static 3D models

Use **Assets > Import > Files / Models** and choose a format filter: glTF/GLB,
FBX, OBJ, Collada, Blender, STL/PLY, or **all Assimp formats**. The full filter is
generated from the bundled Assimp 5.4.3 importers. Support varies by file version
and encoding. Animation playback and skinning are not implemented.

## Directories and destination folders

**Import > Directory (full structure)** copies a folder and all its descendants
into the currently open Assets folder, preserving nested and empty directories.
OS file/directory drops use that same current folder, not the Assets root. Name
collisions receive numbered suffixes; existing files are never overwritten.
Symbolic links and recursive imports into the source are rejected. Limits are
10,000 files, 1 GiB total, and 256 MiB per file. Copy failures remove the incomplete
new destination. Source files/directories are never modified.

Directory imports also convert recognized model sources after copying. Original
models, textures and dependencies remain in the copied structure. Failed model
conversions are reported while retaining copied files and successful conversions.
A single model import bakes geometry and copies supported referenced textures;
external files (such as a glTF .bin) must exist beside the source during import.

## Meshes and materials

New imports create a `.lemesh` model bundle and a sibling `_data` folder containing
separate mesh assets, engine `.mat` files and copied/embedded textures. Node
transforms are baked into vertices while mesh parts/material assignments are
preserved. Normals and the first UV channel are imported; absent normals are
generated. Point/line-only models are rejected. Limits are 500,000 triangles total,
1024 mesh parts and 256 MiB per source/baked geometry file.

Basic diffuse colors, diffuse textures and normal maps are converted. Compressed
PNG/JPEG-compatible embedded textures are copied; uncompressed embedded textures
are written as TGA. Unsupported or missing textures are listed as import warnings
and fall back to material color. This is **not full PBR conversion**: metallic,
roughness, opacity/blending, special shaders, layered/procedural textures and other
source-specific material features are not reproduced. Edit generated `.mat` assets
in Inspector. Models remain static; source cameras/lights are not instantiated.

Drop a bundle into Scene, double-click it, or use **Instantiate in level**. This
creates an **empty root GameObject named after the model** with one child per mesh,
each with its own material reference and fitted static box Collider. No RigidBody
is added. Source node nesting is flattened below the root. Old version-1 `.lemesh`
assets remain supported and instantiate as a root with one mesh child.

## Import settings and Transform scale

Single-click a model in Assets to open **Model import settings** in Inspector.
Set **Import scale** (0.000001–1000000) and click **Apply import scale**. It scales
geometry before the object's Transform, allowing very small models without
fighting the Transform editor's minimum scale. All instances update; associated
Box collider bounds are refitted. Convex hulls rebuild using the new geometry.
Mesh-part assets share their parent bundle's import scale. Changing this shared
asset setting is saved immediately, not recorded as a scene Undo operation.

MeshRenderer's Model asset picker can assign geometry to existing objects. A
single MeshRenderer has one material; instantiate a bundle to retain its separate
materials. Selecting a primitive clears the model reference. GPU geometry is
cached and refreshed after in-editor import-setting changes. External geometry
edits require reimport/project reload. Assets and references travel with Save As,
levels and prefabs; scene formats JSON 14 / legacy text 19 persist convex sources and hull detail.

See [scripting](scripting.md#convex-model-colliders) for `shape = "Convex"` and
model-backed hull sources. Convex colliders simplify source points with a **Hull detail** slider (2–15,
default 8), then generate the hull from the retained points. Lower values simplify
more; the result can cut into visible geometry. Rendered geometry is unchanged.

Assimp's bundled notice is in `data/licenses/assimp.txt`. Model files themselves
remain subject to their creators' licenses.
