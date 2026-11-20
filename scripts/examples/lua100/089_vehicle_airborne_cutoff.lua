-- 089: vehicle airborne cutoff
-- Setup: Vehicle root. Cuts engine command when all wheels lose contact.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Vehicle)
end
function onFixedUpdate(dt)
    if c then c.throttle = c.isGrounded and isKeyDown("W") and 1 or 0 end
end
