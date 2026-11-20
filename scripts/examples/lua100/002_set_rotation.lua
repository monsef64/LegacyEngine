-- 002: set rotation
-- Setup: Attach to a mesh. Angles are degrees, not radians.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    self:setRotation(Vector3(0, 90, 0))
end
