-- 010: clamp height
-- Setup: Attach to a script-driven object, not a dynamic body.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    local p = self:getPosition()
    if p.y < 0 then self:setPosition(p.x, 0, p.z) end
end
