-- 097: day night rotation
-- Setup: Attach to a dynamic DirectionalLight. One revolution per two minutes.
-- Attach this script to one object using its LegacyScript component.

local angle = 0
function onUpdate(dt)
    angle = (angle + dt*3) % 360
    self:setRotation(angle, 20, 0)
end
