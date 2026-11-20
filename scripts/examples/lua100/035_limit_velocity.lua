-- 035: limit velocity
-- Setup: Dynamic body + Collider. Caps total speed at 8 metres/second.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(RigidBody)
end
function onFixedUpdate(dt)
    if not c then return end
    local v = c:getVelocity()
    local speed = math.sqrt(v.x*v.x + v.y*v.y + v.z*v.z)
    if speed > 8 then c:setVelocity(v.x*8/speed, v.y*8/speed, v.z*8/speed) end
end
