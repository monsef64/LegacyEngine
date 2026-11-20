-- 032: configure mass
-- Setup: Add a RigidBody first. Sets mass in kilograms.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local body = self:getComponent(RigidBody)
    if body then body.mass = 5 end
end
