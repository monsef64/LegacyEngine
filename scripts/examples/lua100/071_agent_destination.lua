-- 071: agent destination
-- Setup: NavMeshAgent on baked navmesh. Set reachable coordinates for your level.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local agent = self:getComponent(NavMeshAgent)
    if agent then agent:setDestination(Vector3(8, 0, 8)) end
end
