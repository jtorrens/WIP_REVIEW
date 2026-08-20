-- Synchronize color_enum_labels.json against Fusion 21's native CST.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local fusion = nil
for _ = 1, 60 do
    fusion = bmd.scriptapp("Fusion", "localhost")
    if fusion ~= nil then break end
    bmd.wait(1)
end
if fusion == nil then error("Fusion Standalone is not reachable") end

local catalog = dofile(script_directory() .. "color_enum_catalog.lua")
local result = catalog.sync(fusion)
print(string.format("SHOTCONFIG_COLOR_ENUM_SYNC_OK spaces=%d gammas=%d",
    #result.colorSpaces, #result.gammas))
