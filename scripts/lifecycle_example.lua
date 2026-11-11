-- Run in the in-engine Lua Editor. Component references are live proxies.
local ball
local elapsed = 0

function onStart()
    local previous = findGameObjectByName("Lua Ball")
    if previous then destroyGameObject(previous) end
    ball = createGameObject("Lua Ball") -- starts with Transform + ObjectName
    getComponent(ball, "Transform").position = {x = 0, y = 3, z = 0}
    local renderer = addComponent(ball, "MeshRenderer")
    renderer.mesh = "Sphere"
    renderer.color = {x = 0.2, y = 0.6, z = 1}
    local collider = addComponent(ball, "Collider")
    collider.shape = "Sphere"
    addComponent(ball, "RigidBody")
    ball.onStart = function() print("Ball started") end
    ball.onDestroy = function() print("Ball destroyed") end
end

function onUpdate(deltaTime)
    elapsed = elapsed + deltaTime
end

function onFixedUpdate(deltaTime)
    -- Called immediately before each physics step, not once per render frame.
    -- Example: getComponent(ball, "RigidBody").linearVelocity.x = 0.5
end

function onDestroy()
    print("Script stopped after", elapsed, "seconds")
end
