# Prefab workflow

A prefab is a reusable **`.prefab` asset**, not a component type or a Lua
constructor. It stores a single root GameObject and its descendants, including
components, script attachments and references inside that subtree.

## Create and instantiate

1. Build an object hierarchy in the level.
2. Drag its root from Hierarchy onto the Asset Browser. The browser creates a
   uniquely named `.prefab` file in the current folder and links the original
   object to it. Linked roots and their descendants are blue in Hierarchy.
3. Drag a prefab asset onto Scene, or right-click it and choose **Instantiate in
   level**. You can also call `spawn` from a script.

Asset/mesh references remain project-relative. References to objects outside the
saved subtree are cleared; a prefab does not capture the rest of the level.
Nested asset links are flattened when saving a prefab: this is not a prefab
variant/override system.

## Isolated Prefab Window

Double-click a prefab asset, or choose **Edit Prefab** from its context menu.
The **Prefab Window** controls an isolated edit mode: Hierarchy and Scene now
contain only the prefab, not the level. The level, camera, selection and history
are kept separately. Newly created objects are parented under the prefab root.
Keep exactly one Transform root; saving rejects disconnected roots.

- **Save Prefab** / **Ctrl+S** writes the linked asset. It refreshes linked instances
  in the retained level and affected saved `.level` files in the project's Assets.
- **Exit Prefab Mode** restores normal level editing. Unsaved prefab edits are
  discarded; save first if you want to retain them.
- Play and project/level file actions are disabled during isolation.

Refreshing preserves each instance root's identity, parent and placement
(position/rotation/scale). Other component values and child trees are replaced
from the prefab; child entity identities and instance-specific edits are **not**
preserved. There is no override/Apply/Revert UI. Saving a prefab file is an asset
operation, not a reversible asset-file edit. Save the project/level normally to
persist the refreshed active scene. Missing/broken prefab files report errors.

## Scripting

```legacyscript
function onStart()
    local instance = spawn("Prefabs/Crate.prefab",
                           {x=0, y=2, z=0}, {x=0, y=90, z=0})
    destroy(instance, 5)
end
```

`spawn(source, position, rotation)` accepts a project-relative prefab path or a
live GameObject. Object sources copy the entire child tree without linking the
copy to an asset. Position is world-space; rotation is Euler degrees or a
quaternion. Named-coordinate tables and numeric vector tables are supported.
The new root is returned. Prefab spawns retain the asset link for persistence.
All clones count toward the 10,000-object scene limit.

`destroy(object, delay)` destroys the subtree immediately when delay is omitted
or zero, or after nonnegative playing seconds. Delayed destruction pauses with
Play pause, but is not a wait inside the calling script. Scheduling again replaces
the pending delay. Pending destruction is runtime-only and is not saved.

See the [scripting reference](scripting-reference.md) for API signatures and the
[project guide](projects.md) for level storage.
