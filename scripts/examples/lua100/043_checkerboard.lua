-- 043: checkerboard
-- Setup: Attach to a primitive MeshRenderer.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local mesh = self:getComponent(MeshRenderer)
    if mesh then mesh.checkerboard = true end
end
