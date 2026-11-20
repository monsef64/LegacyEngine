-- 036: stop body
-- Setup: Dynamic body. Press B to clear velocity and queued forces.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(RigidBody)
end
function onUpdate(dt)
    if c and isKeyPressed("B") then c:stop() end
end
