-- 086: vehicle boost
-- Setup: Vehicle root. W drives; hold Left Shift to increase engine force.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Vehicle)
end
function onFixedUpdate(dt)
    if not c then return end
    c.engineForce = isKeyDown("Left Shift") and 14000 or 8000
    c.throttle = isKeyDown("W") and 1 or 0
end
