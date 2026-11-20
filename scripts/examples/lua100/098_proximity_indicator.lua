-- 098: proximity indicator
-- Setup: Attach to a mesh. Create Target; color becomes green within 3 metres.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    local target = findGameObjectByName("Target")
    local mesh = self:getComponent(MeshRenderer)
    if not target or not mesh then return end
    local a, b = self:getPosition(), target:getPosition()
    local near = (a.x-b.x)^2 + (a.y-b.y)^2 + (a.z-b.z)^2 < 9
    mesh:setMaterialColor(near and 0 or 1, near and 1 or 0, 0.1)
end
