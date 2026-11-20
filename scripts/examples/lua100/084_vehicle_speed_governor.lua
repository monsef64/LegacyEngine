-- 084: vehicle speed governor
-- Setup: Vehicle root, wide floor. Demonstrates throttle cutoff plus service braking.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Vehicle)
end
function onFixedUpdate(dt)
    if not c then return end
    c.throttle = c.speed < 12 and 1 or 0
    c.brake = c.speed > 13 and 0.5 or 0
end
