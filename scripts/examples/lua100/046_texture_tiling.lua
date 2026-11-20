-- 046: texture tiling
-- Setup: Attach to a MeshRenderer with an assigned texture.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local mesh = self:getComponent(MeshRenderer)
    if mesh then mesh:setMaterialTiling(4, 4) end
end
