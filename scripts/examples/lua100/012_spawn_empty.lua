-- 012: spawn empty
-- Setup: Attach to an object. Creates one empty object on Play.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local object = createGameObject("Spawned empty")
    object:addComponent(Transform)
    object:setPosition(0, 1, 0)
end
