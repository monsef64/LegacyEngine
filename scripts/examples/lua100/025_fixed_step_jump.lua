-- 025: fixed step jump
-- Setup: Add a dynamic RigidBody and Collider. Space jumps; no ground gate in this basic demo.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(RigidBody)
end
function onFixedUpdate(dt)
    if c and isKeyPressed("Space") then c:addImpulse(0, c.mass * 5, 0) end
end
