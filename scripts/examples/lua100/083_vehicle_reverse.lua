-- 083: vehicle reverse
-- Setup: Vehicle root. Hold S for reverse; release to coast.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Vehicle)
end
function onFixedUpdate(dt)
    if c then c.throttle = isKeyDown("S") and -0.5 or 0 end
end
