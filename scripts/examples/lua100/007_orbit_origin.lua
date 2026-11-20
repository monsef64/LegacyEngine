-- 007: orbit origin
-- Setup: Attach to a decorative object. Orbits world origin at radius 4.
-- Attach this script to one object using its LegacyScript component.

local angle = 0
function onUpdate(dt)
    angle = angle + dt
    self:setPosition(math.cos(angle) * 4, 2, math.sin(angle) * 4)
end
