-- 060: audio zone fade
-- Setup: Attach to a trigger Collider. Put AudioSource on object Music, assign a clip.
-- Attach this script to one object using its LegacyScript component.

function onTriggerEnter(other)
    local music = findGameObjectByName("Music")
    local audio = music and music:getComponent(AudioSource)
    if audio then audio:fadeIn(1) end
end
function onTriggerExit(other)
    local music = findGameObjectByName("Music")
    local audio = music and music:getComponent(AudioSource)
    if audio then audio:fadeOut(1) end
end
