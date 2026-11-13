-- Attach to a Raycast GameObject, point its local -Z toward some Collider objects.
local query
function onStart()
    query = self:getComponent(Raycast) or self:addComponent(Raycast)
    query.type = Raycast.LineOfSight
    query.angle = 180
    query.samples = 41
    query.distance = 15
    query:setLayers({"Default"})
end
function onUpdate(dt)
    local hit = query:cast()
    if hit and isKeyPressed("E") then
        debug.log("Interact", hit.collider.gameObject:getName(), hit.distance)
    end
end
-- Add a Collider and enable Is trigger to use these callbacks on this object.
function onTriggerEnter(collider)
    if collider then debug.log("Enter", collider.gameObject:getName()) end
end
function onTriggerStay(collider)
    -- collider.transform and collider.gameObject:getComponent(...) are live handles.
end
function onTriggerExit(collider)
    debug.log("Exit", collider and collider.gameObject:getName() or "destroyed")
end
