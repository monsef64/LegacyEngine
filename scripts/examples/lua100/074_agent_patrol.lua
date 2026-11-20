-- 074: agent patrol
-- Setup: Baked navmesh; adjust these two points to reachable positions.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(NavMeshAgent)
end
local index, timer = 1, 0
local points = {Vector3(2,0,2), Vector3(10,0,2)}
function onUpdate(dt)
    timer = timer - dt
    if c and timer <= 0 and not c.hasPath and not c.pathPending then
        c:setDestination(points[index]); index = 3-index; timer = 1
    end
end
