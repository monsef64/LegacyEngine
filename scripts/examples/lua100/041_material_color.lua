-- 041: material color
-- Setup: Attach to a MeshRenderer. Uses instance overrides, not asset edits.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local mesh = self:getComponent(MeshRenderer)
    if mesh then mesh:setMaterialColor(0.2, 0.7, 1) end
end
