-- 048: spot cone
-- Setup: Attach to a dynamic SpotLight.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local light = self:getComponent(SpotLight)
    if light then light:setSpotAngles(15, 35); light:setRange(20) end
end
