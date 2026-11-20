-- 085: vehicle parking brake
-- Setup: Vehicle root. Space toggles parking brake; W drives when released.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Vehicle)
end
local parked = true
function onFixedUpdate(dt)
    if not c then return end
    if isKeyPressed("Space") then parked = not parked end
    c.brake = parked and 1 or 0; c.handbrake = parked
    c.throttle = not parked and isKeyDown("W") and 1 or 0
end
