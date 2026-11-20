-- 053: audio fade
-- Setup: AudioSource with assigned clip. F fades in, G fades out.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(AudioSource)
end
function onUpdate(dt)
    if not c then return end
    if isKeyPressed("F") then c:fadeIn(2) end
    if isKeyPressed("G") then c:fadeOut(2) end
end
