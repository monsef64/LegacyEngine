-- 050: reset material
-- Setup: MeshRenderer with instance overrides. R restores its asset appearance.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(MeshRenderer)
end
function onUpdate(dt)
    if c and isKeyPressed("R") then c:resetMaterialOverrides() end
end
