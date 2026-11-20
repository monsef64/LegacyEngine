-- 006: bobbing
-- Setup: Attach to a decorative object. Remembers its starting height.
-- Attach this script to one object using its LegacyScript component.

local origin, time
function onStart() origin = self:getPosition(); time = 0 end
function onUpdate(dt)
    time = time + dt
    self:setPosition(origin.x, origin.y + math.sin(time * 2) * 0.5, origin.z)
end
