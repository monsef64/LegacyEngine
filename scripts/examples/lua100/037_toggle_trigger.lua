-- 037: toggle trigger
-- Setup: Attach to a Collider. T switches solid/sensor behavior.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Collider)
end
function onUpdate(dt)
    if c and isKeyPressed("T") then c.isTrigger = not c.isTrigger end
end
