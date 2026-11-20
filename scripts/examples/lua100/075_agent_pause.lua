-- 075: agent pause
-- Setup: Agent following a route. Space pauses/resumes without clearing it.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(NavMeshAgent)
end
function onUpdate(dt)
    if c and isKeyPressed("Space") then c.stopped = not c.stopped end
end
