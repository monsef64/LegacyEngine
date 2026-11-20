-- 013: follow target
-- Setup: Create Target; attach to a non-physics follower.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    local target = findGameObjectByName("Target")
    if target then
        local p = target:getPosition()
        self:setPosition(p.x, p.y + 2, p.z + 3)
    end
end
