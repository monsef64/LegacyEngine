-- 024: arrow movement
-- Setup: Attach to a non-physics object. Focus Game; use arrow keys.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    local x = (isKeyDown("Right") and 1 or 0) - (isKeyDown("Left") and 1 or 0)
    local z = (isKeyDown("Down") and 1 or 0) - (isKeyDown("Up") and 1 or 0)
    local length = math.sqrt(x*x + z*z)
    if length > 0 then self:translate(x/length*3*dt, 0, z/length*3*dt) end
end
