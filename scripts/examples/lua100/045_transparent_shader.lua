-- 045: transparent shader
-- Setup: Attach to a MeshRenderer. Choose opacity separately in its material.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local mesh = self:getComponent(MeshRenderer)
    if mesh then mesh:setMaterialShader("Diffuse Transparent") end
end
