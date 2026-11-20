-- 056: audio 2d
-- Setup: AudioSource with assigned clip. Configure as non-spatial music.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local audio = self:getComponent(AudioSource)
    if audio then audio:setSpatialBlend(0); audio:setVolume(0.4); audio:setLooping(true); audio:play() end
end
