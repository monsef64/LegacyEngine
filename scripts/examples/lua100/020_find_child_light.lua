-- 020: find child light
-- Setup: Attach to a parent with a PointLight descendant. Search includes self.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local light = self:getComponentInChildren(PointLight)
    if light then light:setIntensity(2) end
end
