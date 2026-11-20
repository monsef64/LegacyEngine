-- 057: audio 3d
-- Setup: AudioSource with clip; active AudioListener needed nearby.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local audio = self:getComponent(AudioSource)
    if audio then audio:setSpatialBlend(1); audio:setDistance(15); audio:play() end
end
