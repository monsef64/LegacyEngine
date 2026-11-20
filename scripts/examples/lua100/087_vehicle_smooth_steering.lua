-- 087: vehicle smooth steering
-- Setup: Vehicle root. A/D steering eases toward input; W accelerates.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Vehicle)
end
local steering = 0
function onFixedUpdate(dt)
    if not c then return end
    local target = (isKeyDown("D") and 1 or 0) - (isKeyDown("A") and 1 or 0)
    steering = steering + (target-steering)*(1-math.exp(-5*dt))
    c.steering = steering; c.throttle = isKeyDown("W") and 1 or 0
end
