-- 096: sliding door
-- Setup: Attach to a non-physics door. E toggles a smooth 3-metre opening.
-- Attach this script to one object using its LegacyScript component.

local origin, open, offset = nil, false, 0
function onStart() origin = self:getPosition() end
function onUpdate(dt)
    if isKeyPressed("E") then open = not open end
    offset = offset + ((open and 3 or 0)-offset)*(1-math.exp(-4*dt))
    self:setPosition(origin.x, origin.y+offset, origin.z)
end
