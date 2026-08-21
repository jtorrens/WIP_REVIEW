-- Live, readable path previews for ShotConfig.
-- Called by Fusion 21's INPS_ExecuteOnChange hook whenever an input affecting
-- a path is edited.  Keeping the result as a value (rather than an expression
-- on the visible TextEdit) prevents the Inspector from displaying Lua source.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local apply = dofile(script_directory() .. "apply_shot_config.lua")
local M = {}

local function input(tool, name)
    local ok, value = pcall(function() return tool:FindInput(name) end)
    if ok and value ~= nil then return value end
    return tool[name]
end

local function write(tool, name, value, time)
    local target = input(tool, name)
    if target == nil then return false, "control '" .. name .. "' is missing" end
    local ok, failure = pcall(function() target[time] = value end)
    if not ok then return false, tostring(failure) end
    return true
end

function M.run(config, time)
    if config == nil then return false, "ShotConfig is unavailable" end
    local active_comp = config.Comp()
    time = time or (active_comp and active_comp.CurrentTime) or 0
    local values, err = apply.read_config(config, time)
    if values == nil then return false, err end

    for _, kind in ipairs({ "Loader", "Saver" }) do
        for index = 1, apply.TARGET_SLOT_COUNT do
            local _, template_control, resolved_control = apply.target_controls(kind, index)
            local template = input(config, template_control)[time]
            local resolved, resolve_error = apply.resolve_template(template, values)
            if resolved == nil then resolved = "Invalid template: " .. resolve_error end
            local written, write_error = write(config, resolved_control, resolved, time)
            if not written then return false, write_error end
        end
    end
    return true
end

return M
