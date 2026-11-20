-- 076: agent sprint
-- Setup: Moving agent; hold Left Shift in Game.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(NavMeshAgent)
end
function onUpdate(dt)
    if c then c.speed = isKeyDown("Left Shift") and 6 or 2 end
end
