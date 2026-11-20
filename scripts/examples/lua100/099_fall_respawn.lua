-- 099: fall respawn
-- Setup: Dynamic body + Collider. Falling below Y=-10 resets to its starting point.
-- Attach this script to one object using its LegacyScript component.

local checkpoint, body
function onStart() checkpoint = self:getPosition(); body = self:getComponent(RigidBody) end
function onFixedUpdate(dt)
    if self:getPosition().y < -10 then
        if body then body:stop(); body:movePosition(checkpoint.x,checkpoint.y,checkpoint.z)
        else self:setPosition(checkpoint) end
    end
end
