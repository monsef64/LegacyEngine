-- 091: health damage
-- Setup: Attach to a dynamic Collider. Each new collision deals 10 damage.
-- Attach this script to one object using its LegacyScript component.

local health = 100
function onCollisionEnter(other)
    health = math.max(0, health-10)
    debug.log("Health:", health)
    if health == 0 then self:destroy() end
end
