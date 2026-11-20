-- 080: agent status
-- Setup: Agent on a baked navmesh. Logs status once per second.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(NavMeshAgent)
end
local timer = 0
function onUpdate(dt)
    timer = timer + dt
    if c and timer >= 1 then timer = timer - 1; debug.log(c.status, c.remainingDistance) end
end
