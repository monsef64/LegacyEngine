-- 033: launch impulse
-- Setup: Dynamic body + Collider. One launch at Play start.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local body = self:getComponent(RigidBody)
    if body then body:addImpulse(body.mass * 2, body.mass * 6, 0) end
end
