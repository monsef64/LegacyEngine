-- 008: pulse scale
-- Setup: Attach to a decorative object; no physics driver.
-- Attach this script to one object using its LegacyScript component.

local time = 0
function onUpdate(dt)
    time = time + dt
    local size = 1 + math.sin(time * 3) * 0.2
    self:setScale(size, size, size)
end
