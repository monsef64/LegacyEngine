-- 077: agent arrival
-- Setup: Agent controlled by another destination script or command.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(NavMeshAgent)
end
local wasMoving = false
function onUpdate(dt)
    if not c then return end
    if wasMoving and not c.hasPath and c.status == "Arrived" then debug.log("Reached destination") end
    wasMoving = c.hasPath
end
