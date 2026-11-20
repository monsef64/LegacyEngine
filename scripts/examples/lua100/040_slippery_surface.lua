-- 040: slippery surface
-- Setup: Attach to a floor with Collider. No RigidBody needed on the floor.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local collider = self:getComponent(Collider)
    if collider then collider.friction = 0.05; collider.bounciness = 0 end
end
