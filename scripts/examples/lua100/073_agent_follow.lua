-- 073: agent follow
-- Setup: Baked navmesh and Target object on it. Requests a route twice per second.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(NavMeshAgent)
end
local timer = 0
function onUpdate(dt)
    timer = timer - dt
    if c and timer <= 0 then
        timer = 0.5
        local target = findGameObjectByName("Target")
        if target then c:setDestination(target:getPosition()) end
    end
end
