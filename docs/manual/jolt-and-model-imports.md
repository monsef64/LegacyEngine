# Jolt migration and restricted model imports

## Scope

ReactPhysics3D is replaced by statically linked **Jolt Physics 5.3.0**. The public
Physics API, ECS component names, serialized ColliderShape values, Lua physics
functions and existing scene assets remain in place. Existing numeric tuning may
need adjustment: a different solver is not numerically identical to the old one.

The engine remains **OpenGL 3.3**. FSR 3 integration, Rendering controls and script
APIs are deliberately deferred until a compatible renderer/temporal pipeline exists;
no placeholder FSR controls are added.

## Physics behavior

- Fixed 60 Hz simulation, bounded frame catch-up and interpolated render poses.
- Static, dynamic, mass-zero and kinematic bodies; gravity, friction, restitution,
  damping, velocity changes, forces and impulses. Game-written pose changes teleport
  only changed position coordinates, preserving interpolation on untouched axes.
- Body/component add/remove, object activity, asset-root changes, reset and destruction
  synchronize with the solver. Invalid component values are excluded. Failed shape
  construction reports an error and waits for changed settings/model revision.
- Box, Sphere and Capsule use Jolt primitives. Cone uses a 32-sided convex cone;
  triangle-mesh colliders preserve actual triangles, concavity and holes. Meshes
  remain static even when a RigidBody is present, with no silent convex/box fallback.
- CharacterController continues to use the engine's existing controller/query logic
  on top of rigid bodies, not a newly exposed Jolt CharacterVirtual component. Its
  body has translation-only degrees of freedom to remain upright.
- Collision/trigger enter/stay/exit events are per entity pair, emitted in both
  directions. Sleeping cached solid contacts are retained as gameplay contacts;
  structural/static-pose/material changes wake moving bodies. Explicit shape queries
  also detect stationary primitive triggers against stationary colliders.
- Concave mesh sensors can query convex primitives; **concave mesh-vs-mesh overlap
  is not supported by Jolt's narrow phase**. Use primitive trigger volumes for that
  case. Static mesh-vs-mesh solid contact is not simulated.
- Ray queries use the Jolt broad/narrow phase, honor the caller filter and return
  nearest-first results, with one nearest hit per entity for raycastAll. Starting
  inside a convex collider does not count as an entry hit. Sphere/box overlap queries
  use actual geometry rather than an AABB or convex replacement for mesh colliders.
- Jolt simulation jobs use a bounded worker pool; contact callbacks serialize writes
  to their contact maps, while ECS synchronization and event publication remain on
  the main thread after the solver joins. Current capacity
  is 16,384 bodies/contact constraints and 65,536 candidate body pairs. Overflow
  reports an error. Jolt memory allocation/type registration are initialized once
  and shared across Physics instances, including reset operations.

## Model imports

Assimp is built with **OBJ and glTF importers only**. Source conversion accepts
`.obj`, `.gltf` and `.glb`, including glTF skeletal animation channels, skin weights,
multiple meshes/materials and animation wrappers. Existing baked `.lemesh` assets
from other formats remain readable without the original importer. Morph/vertex
animation remains unsupported; source conversion reports an error rather than
claiming successful skeletal-only conversion of that data.

File-dialog labels match the restricted formats. Other arbitrary files may still
be copied as generic project assets; they are not converted as models. Cached
Assimp importer options from older configurations are disabled explicitly so that
reconfiguring an existing build does not retain extra format implementations.

## Inspector

**Add Component → Collider → Box / Sphere / Capsule / Cone / Mesh** creates one
Collider with that shape selected. A missing Transform is added. The submenu is
disabled when a Collider already exists, since shapes share one ECS component.
Change an existing shape in its Collider Inspector instead. Component Reset still
restores the default Box collider values.

## Manual verification checklist

No automated tests are added or run. Before merging/releasing, verify on Linux and
Windows MinGW-w64 with representative existing projects:

1. Drop each primitive on a floor, stack/sleep bodies, move/delete the floor,
   change friction/restitution, resize/recenter shapes, and edit sleeping bodies.
2. Toggle gravity, mass zero/nonzero, kinematic state and object activity. Apply
   forces/impulses in onFixedUpdate, including catch-up frames with multiple steps.
   Check interpolated movement and scripted per-axis position changes.
3. Walk/jump/sprint a CharacterController on floors, slopes and mesh stairs;
   verify grounding, ceiling/wall constraints and no unwanted body rotation.
4. Check trigger enter/stay/exit on stationary objects and moving objects, disabled
   objects, removed shapes and destroyed entities. A resting solid contact must
   not send Exit merely because its body sleeps. Check both participants' callbacks.
5. Import a glTF with concave holes, assign a Mesh collider, and check raycasts,
   sphere/box queries and dynamic primitives against it. Verify reflected/nonuniform
   scale, collider center, mesh-detail changes and no convex/box fallback.
6. Verify nearest/all ray queries with ignored layers and rays originating inside
   primitives; verify overlap filtering and duplicate triangle-hit suppression.
7. Stop/start Play repeatedly, open another project and create/reset multiple Physics
   instances. Check invalid/missing collider assets produce useful diagnostics.
8. Import OBJ with MTL/textures and both embedded/external-buffer animated glTF/GLB;
   verify clips, skinning, material grouping, preview and serialized assets. Confirm
   FBX/DAE/STL are not offered or converted and old baked assets still open.
9. Add each Collider shape from the submenu, reset it, and Undo/Redo the Inspector
   action. Confirm adding another Collider is disabled rather than replacing one.

Graphical/physics runtime behavior, solver tuning and Windows compatibility require
manual verification; a successful headless compile/link alone does not establish
runtime correctness or a measured performance gain.
