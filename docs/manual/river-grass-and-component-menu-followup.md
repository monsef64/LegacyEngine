# Grass, river and component-menu follow-up

## Grass orientation

Crossed grass cards now use V=0 at their base and V=1 at their tips to match the material loader's OpenGL row flip. Tree-model UVs and existing texture files are unchanged.

## River water

The repeating sine-wave bands have been replaced by two-scale drifting noise, subtle normal detail, Fresnel sky tint, directional highlights and broken-up bank foam. Geometry stays cached; no extra reflection targets or depth copies are introduced. This is lightweight approximate shading, not scene reflection/refraction or fluid simulation. Performance and appearance still need desktop verification.

## Scene curve editing

Select a River and enable **Edit river curve** in the Scene toolbar. The Inspector continues to edit the same points.

- Use **+ Point** to extend the curve or **- Point** to remove a point.
- Drag numbered handles along the horizontal plane; hold Ctrl to change height.
- Shift-click the Scene to append on the last point's horizontal plane.
- Right-click a point for insert/delete actions.
- Curves retain the 2–64-point limit and editor undo history.

## Component organization

Add Component and Create menus now group Clouds, Fog, River and Terrain under **Environment**; lights under **Lights**; and FPS camera under **Cameras > FPS**. CameraEffects displays as **Camera Effects**. These are editor display names: existing serialized and Lua component identifiers remain unchanged.

## Manual verification

- Paint existing grass samples and confirm bases touch the terrain and tips face upward.
- Inspect water near/far and under different sun directions; check banks and sloped ribbons.
- Add, drag, height-drag, insert and delete curve points; check undo/redo and save/reopen.
- Confirm point context menus do not activate Scene freecam; verify normal freecam away from handles.
- Verify Inspector edits and Scene handles remain synchronized.
- Check all reorganized menus and reload existing scenes/scripts.

No automated tests or interactive desktop validation were performed for this follow-up.

## Cloud boundaries, orbit and optional grass billboards

Cloud limits now fade independently along each box axis, softening the outer
corners and boundaries without smoothing the cloud noise itself (2D and volumetric).

Alt-left-drag in the Scene locks onto the selected Transform's world-space origin
for the entire gesture, regardless of component type, including particle systems.
With no Transform selected it uses the current camera target, not a stale selection.
The gizmo uses the same camera up-vector as the Scene, including axis-aligned views.

Terrain tools > Grass > **Grass type** offers **Crossed cards** (the unchanged
default) and **Billboard**. Billboards use a single upright camera-facing card,
retain wind/alpha cutout/instancing, and are configured per prototype. Existing
terrain assets default to crossed cards when the new optional field is absent.
Tree rendering is unchanged.

Manual checks: orbit particles, offset models, empty transforms and parented objects;
repeat from top/bottom axis views and compare gizmo alignment. Switch grass types,
orbit and enter Play, then save/reopen terrain. Inspect cloud box corners from
inside and outside in both modes. Desktop checks remain pending.
