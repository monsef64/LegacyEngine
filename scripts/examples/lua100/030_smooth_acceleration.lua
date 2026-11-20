-- 030: smooth acceleration
-- Setup: Attach to a non-physics object. Hold W in Game.
-- Attach this script to one object using its LegacyScript component.

local speed = 0
function onUpdate(dt)
    local target = isKeyDown("W") and 5 or 0
    speed = speed + (target - speed) * (1 - math.exp(-6 * dt))
    self:translate(0, 0, -speed * dt)
end
