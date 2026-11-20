-- 034: held force
-- Setup: Dynamic body + Collider. Hold F; force is applied each fixed step.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(RigidBody)
end
function onFixedUpdate(dt)
    if c and isKeyDown("F") then c:addForce(0, c.mass * 15, 0) end
end
