-- 072: agent command
-- Setup: Baked navmesh; set a reachable destination below. Space queues movement.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(NavMeshAgent)
end
function onUpdate(dt)
    if c and isKeyPressed("Space") then c:setDestination(10, 0, 5) end
end
