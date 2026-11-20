-- 081: vehicle keyboard
-- Setup: Attach to Create > Vehicle root above a collidable floor. WASD, Space, Shift.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Vehicle)
end
function onFixedUpdate(dt)
    if not c then return end
    c.keepUpright = true
    c.throttle = (isKeyDown("W") and 1 or 0) - (isKeyDown("S") and 1 or 0)
    c.steering = (isKeyDown("D") and 1 or 0) - (isKeyDown("A") and 1 or 0)
    c.brake = isKeyDown("Space") and 1 or 0
    c.handbrake = isKeyDown("Left Shift")
end
