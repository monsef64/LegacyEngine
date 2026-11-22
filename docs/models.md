# Importing static and animated 3D models

Use **Assets > Import > Files / Models** for OBJ, glTF/GLB, or Half-Life 1
GoldSrc MDL (IDST version 10). OBJ imports geometry/materials; skeletal glTF and
GoldSrc MDL imports also preserve supported bones and animation clips. Blender,
Quake MDL and Source-engine MDL are not supported. Keep companion files next to
the source during import, including external glTF buffers/textures and GoldSrc
texture/sequence-group MDL files. The importer uses the BSD-licensed Assimp dependency.

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

New imports create one `.mesh` container with virtual mesh, material, texture and
animation subassets. Expand the model's arrow in the Asset Browser to access them;
no generated folder or sibling material/texture files remain after a successful
single-model conversion. Older `.lemesh`/`.mesh` separate-file layouts still load.
Container references use `Model.mesh#Member.ext`; move/copy/rename the owner rather
than trying to move a virtual member separately.

Mesh parts and material assignments are preserved. Normals and the first UV
channel are imported; absent normals are generated. Point/line-only models are
rejected. Limits include 500,000 triangles total, 1024 mesh parts, 256 MiB per source
and 256 MiB of packed JSON, with a 32 MiB encoded-image budget per texture.

Basic diffuse colors, diffuse textures and normal maps are converted. Compressed
PNG/JPEG-compatible embedded textures are copied; uncompressed embedded textures
are written as TGA. Unsupported or missing textures are listed as import warnings
and fall back to material color. This is **not full PBR conversion**: metallic,
roughness, opacity/blending, special shaders, layered/procedural textures and other
source-specific material features are not reproduced. Edit virtual `.mat` subassets
in Inspector. Source cameras/lights are not instantiated. Imported clips use the
Animation component and existing cross-fades; no Animator/AnimatorSheet or additive
blending is introduced. GoldSrc game-specific sequence events, bodygroup switching,
blend controllers, attachments and hitboxes are not converted to gameplay behavior.

Drop a bundle into Scene, double-click it, or use **Instantiate in level**. This
creates an **empty root GameObject named after the model** with one child per mesh,
each with its own material reference. No Collider or RigidBody is added; add
collision components explicitly when needed. Source node nesting is flattened below the root. Old version-1 `.lemesh`
assets remain supported and instantiate as a root with one mesh child.

## Import settings and Transform scale

Single-click a model in Assets to open **Model import settings** in Inspector.
Set **Import scale** (0.000001–1000000) and click **Apply import scale**. It scales
geometry before the object's Transform, allowing very small models without
fighting the Transform editor's minimum scale. All instances update; associated
Box collider bounds are refitted. Mesh colliders rebuild using the new geometry.
Mesh-part assets share their parent bundle's import scale. Changing this shared
asset setting is saved immediately, not recorded as a scene Undo operation.

MeshRenderer's Model asset picker can assign geometry to existing objects. A
single MeshRenderer has one material; instantiate a bundle to retain its separate
materials. Selecting a primitive clears the model reference. GPU geometry is
cached and refreshed after in-editor import-setting changes. External geometry
edits require reimport/project reload. Assets and references travel with Save As,
levels and prefabs; scene formats JSON 14 / legacy text 19 persist mesh sources and detail (old convex settings migrate).

See [scripting](scripting.md#mesh-colliders) for `shape = "Mesh"` and
static triangle-mesh sources. The **Mesh detail** slider (2–15, default 8)
clusters vertices at lower values; 15 preserves original triangles. Simplification
can remove small details. Rendered geometry is unchanged.

Blender `.blend` import is disabled in Assimp and rejected by the asset importer,
including directory imports that contain Blender files. Export to glTF/GLB or OBJ first. Cached, fixed-size model icons frame the full geometry independent
of import scale. Hover previews appear after 1.5 seconds.

Assimp's bundled notice is in `data/license/assimp.txt`. Model files themselves
remain subject to their creators' licenses.


### GoldSrc sequence compatibility

GoldSrc animation durations are derived from frame-key times and sequence FPS,
working around the Assimp 5.4 HL1 duration convention. Pose/zero-FPS sequences use
a bounded fallback frame interval. Up to 2048 MDL blend clips are accepted under
the existing total-key and packed-size budgets. Missing companion-file/import
errors identify the source model and explain the main-model/companion layout.
Actual model-specific failures still require the import error and source files to
diagnose; an MDL extension alone does not identify a compatible GoldSrc file.
