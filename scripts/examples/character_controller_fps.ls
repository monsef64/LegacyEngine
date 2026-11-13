-- Attach this script to a Character Controller GameObject at (0, 2, 0).
-- Create a Plane with its Collider below it, then Play and focus the Game view.
-- WASD: move | Shift: sprint | Space: jump | mouse: look | Escape: release cursor
local controller
local camera

function onStart()
    controller = self:getComponent(CharacterController)
    if not controller then controller = self:addComponent(CharacterController) end
    controller.speed = 4.5
    controller.jumpSpeed = 6
    controller.sprintMultiplier = 1.7

    camera = Camera.new("Player FPS Camera")
    local fps = camera:addComponent(FPSCamera)
    fps:linkToCharacter(controller, {x=0, y=0.7, z=0})
    fps.isFreecam = false
    fps.sensitivity = 0.12
    camera:setFieldOfView(75)
    camera:setNearClipPlane(0.05)
    camera:setActive(true)
    lockCursor()
    hideCursor()
end

function onUpdate(deltaTime)
    if isKeyPressed("Escape") then
        unlockCursor()
        showCursor()
    end
    if isMouseButtonPressed("left") then
        lockCursor()
        hideCursor()
    end
    if not isCursorLocked() then
        controller:move(0, 0)
        controller:sprint(false)
        return
    end
    local x, z = 0, 0
    if isKeyDown("A") then x = x - 1 end
    if isKeyDown("D") then x = x + 1 end
    if isKeyDown("W") then z = z - 1 end -- forward is local -Z
    if isKeyDown("S") then z = z + 1 end
    controller:move(x, z) -- direction, NOT displacement; no deltaTime multiplication
    controller:sprint(isKeyDown("Left Shift"))
    if isKeyPressed("Space") then controller:jump() end
    -- Read-only state: isGrounded, isJumping, isMoving, isSprinting.
    -- Writable gates: canMove, canJump, canSprint.
end

function onDestroy()
    unlockCursor()
    showCursor()
end
