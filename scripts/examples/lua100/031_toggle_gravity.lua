-- 031: toggle gravity
-- Setup: Dynamic RigidBody + Collider. G toggles gravity.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(RigidBody)
end
function onUpdate(dt)
    if c and isKeyPressed("G") then c.useGravity = not c.useGravity end
end
