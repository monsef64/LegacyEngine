-- Attach this LegacyScript to an active GameObject. No UI assets are required.
-- Unlock the player cursor and focus Game to interact.
local supplies = 0
local name, volume, showDetails = "Player", 0.5, true

function onStart()
    GUI.onDraw(function()
        GUI.setNextWindowSize(320, 260)
        if GUI.beginWindow("Expedition") then
            GUI.text("Welcome, " .. name)
            name = GUI.inputText("Name", name)
            volume = GUI.sliderFloat("Volume", volume, 0, 1)
            showDetails = GUI.checkbox("Details", showDetails)
            if GUI.button("Collect supply") then
                supplies = supplies + 1
            end
            GUI.sameLine()
            if GUI.button("Reset") then supplies = 0 end
            GUI.separator()
            if showDetails then
                GUI.bulletText("Supplies: " .. supplies)
                GUI.progressBar(math.min(supplies / 10, 1))
            end
        end
        -- Required even when beginWindow returned false (collapsed window).
        GUI.endWindow()
    end)
end
