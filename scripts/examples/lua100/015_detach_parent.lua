-- 015: detach parent
-- Setup: Attach to a child object.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    self:setParent(nil)
end
