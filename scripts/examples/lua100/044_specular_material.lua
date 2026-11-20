-- 044: specular material
-- Setup: Attach to a MeshRenderer lit by a light.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local mesh = self:getComponent(MeshRenderer)
    if mesh then mesh:setMaterialSpecular(0.8) end
end
