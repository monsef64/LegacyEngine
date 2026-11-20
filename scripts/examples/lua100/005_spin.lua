-- 005: spin
-- Setup: Attach to a decorative object without physics.
-- Attach this script to one object using its LegacyScript component.

local yaw = 0
function onUpdate(dt)
    yaw = (yaw + 90 * dt) % 360
    self:setRotation(0, yaw, 0)
end
