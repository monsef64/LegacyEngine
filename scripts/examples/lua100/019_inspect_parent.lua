-- 019: inspect parent
-- Setup: Attach to a child object.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local parent = self:getParent()
    if parent then debug.log("Parent:", parent:getName()) end
end
