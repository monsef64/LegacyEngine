-- 003: nonuniform scale
-- Setup: Attach to a sphere or mesh; no RigidBody needed.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    self:setScale(Vector3(2, 0.5, 1))
end
