# Decal component

Add **Decal** from the Inspector's Add Component menu. A Transform is added if
needed. Assign a project-relative image texture using the texture field or drag
an image asset onto it. PNG with alpha is suitable for marks, signs and splashes.

The decal is a **box projector**: local XY supplies image coordinates and local Z
is projection depth. Move the box so it intersects a surface, rotate it to face the
surface and adjust Width, Height and Projection depth. Transform scaling is also
applied. Keep depth small to avoid projecting through onto unwanted surfaces.
Tint and Opacity modulate the texture. Higher Sorting order draws later for overlap;
equal orders retain ECS traversal order. Enabled and effective object activity gate
rendering. No collider, physics raycast, additional scene render or reflection pass
is needed.

Receivers are visible **opaque depth-writing geometry**, including terrain and
animated meshes. Transparent geometry, sky and particles do not receive decals.
Projection is unlit alpha blending (not a PBR material/deferred normal-map decal).
The projection box can affect multiple opaque surfaces within its depth, including
back-facing surfaces; there is no per-receiver layer or normal-angle filter yet.
Each camera copies opaque depth once when at least one visible decal exists, then
composites visible projectors. Avoid hundreds of overlapping screen-filling decals.
Decals do not write depth, cast shadows or change collision geometry.

Scene/prefab saves, duplication, undo, reset/remove and Play/Stop rollback preserve
Decal values. New saves use the version-37 legacy text schema; existing projects
remain readable. The normal JSON/Zstandard project representation is retained.

```lua
function onStart()
    local mark = self:getComponent(Decal) or self:addComponent(Decal)
    mark.texture = "Textures/Impact.png"
    mark.width = 0.5
    mark.height = 0.5
    mark.depth = 0.2
    mark.opacity = 0.8
    mark.color = {x = 1, y = 0.8, z = 0.5}
    mark.sortingOrder = 1
end
```

Scripting exposes enabled, texture, width, height, depth, color, opacity and sortingOrder. Color reads return a copy; assign the whole
vector to update it. Values are validated and asset paths must stay
project-relative. `@export mark Decal` references an existing component.
