-- 059: audio loop
-- Setup: AudioSource with assigned clip.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local audio = self:getComponent(AudioSource)
    if audio then audio:setLooping(true); audio:play() end
end
