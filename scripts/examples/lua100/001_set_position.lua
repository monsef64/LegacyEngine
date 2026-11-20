-- 001: set position
-- Setup: Attach to any object with Transform. Sets world position once.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    self:setPosition(Vector3(0, 2, 5))
end
