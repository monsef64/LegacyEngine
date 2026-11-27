# Scene component icons and prefab persistence — manual desktop checks

No automated tests were added or run for this change.

1. In Scene, create Transform-only and name-only objects (no icon), then objects with cameras, lights, audio, mesh, navigation and script components. The first non-metadata component in Inspector order determines the badge. Reorder components and check the icon changes.
2. Parent a child at the same position. Confirm the parent's icon wins overlapping screen positions; moving the child apart reveals its icon. Confirm offscreen/behind-camera and inactive objects have no badges. Test perspective and orthographic views.
3. Click an unselected badge, then drag the selected object's transform handles. Confirm icon clicking selects only the intended object and does not move it, while selected-object handles remain usable. Camera preview/orientation overlays stay usable. Game/export must contain no editor badges.
4. Open Add Component and verify the submenu reads Camera (not Cameras). Object-creation menus are intentionally unchanged.
5. Open a prefab in Prefab Mode, drop a multi-part model into it, add nested children, Save Prefab, Exit, and reopen. Confirm the file has one root and includes the complete model/child hierarchy.
6. Add children to a linked instance in a level. Select either its root or a descendant and use Save to Prefab. Spawn a new copy and inspect existing linked copies/saved levels; all should include the new descendants. Verify original instance placement and hierarchy survive propagation.
7. Assign a prefab asset to an Inspector global Prefab field on a linked instance, deliberately different from the template default. Save the project, close, reopen, and inspect the same field. Confirm it retains the exact asset path. Repeat for script attachments on child objects and additional script attachments.
8. Save an instance-only child without applying it to the prefab; reopen the level and verify the child remains. Repeat through editor Play/Stop, level transition/preloading and freshly exported-player startup. Loading must not silently replace serialized instance data.
9. Missing/corrupt prefab links should report errors rather than wiping a saved tree. Check that saving an invalid prefab hierarchy reports an error and does not silently truncate children.

Native desktop visual/runtime verification remains required. Saved scenes are now authoritative on load; explicit prefab saves propagate template changes.
