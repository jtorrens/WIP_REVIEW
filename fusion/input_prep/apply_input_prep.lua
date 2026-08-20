-- Resolves explicit InputPrep targets and applies ShotConfig transactionally.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local SCRIPT_DIR = script_directory()
local geometry = dofile(SCRIPT_DIR .. "geometry.lua")
local shot_apply = dofile(SCRIPT_DIR .. "../shot_config/apply_shot_config.lua")
local M = {}

M.ROLE = "InputPrep"
M.CONFIG_ROLE = "InputPrepConfig"
M.SCHEMA_VERSION = 1
M.TARGET_SLOT_COUNT = 5
M.CONTROL = {
    apply = "IPC_Apply",
    status = "IPC_Status",
}

function M.target_control(index)
    if type(index) ~= "number" or index < 1 or index > M.TARGET_SLOT_COUNT then
        error("target slot index is outside configured range")
    end
    return "IPC_Target" .. tostring(index) .. "Node"
end

local function trim(value)
    return tostring(value or ""):match("^%s*(.-)%s*$")
end

local function log(message)
    print("[InputPrep] " .. message)
end

local function metadata(tool, key)
    local ok, value = pcall(function() return tool:GetData("InputPrep." .. key) end)
    if ok then return value end
    return nil
end

local function input(tool, id)
    if tool == nil then return nil end
    local ok, value = pcall(function() return tool[id] end)
    if ok then return value end
    return nil
end

local function read(tool, id, time)
    local target = input(tool, id)
    if target == nil then return nil, "control '" .. id .. "' is missing" end
    local ok, value = pcall(function() return target[time] end)
    if not ok then return nil, "unable to read '" .. id .. "': " .. tostring(value) end
    return value
end

local function write(tool, id, value, time)
    local target = input(tool, id)
    if target == nil then return false, "control '" .. id .. "' is missing" end
    local ok, failure = pcall(function() target[time] = value end)
    if not ok then return false, tostring(failure) end
    return true
end

local function set_status(config, message, time)
    local ok = write(config, M.CONTROL.status, message, time)
    if not ok then log("WARNING: unable to update InputPrepConfig status") end
end

function M.find_config(comp)
    if comp == nil then return nil, "no active composition" end
    local matches = {}
    for _, tool in pairs(comp:GetToolList(false) or {}) do
        if metadata(tool, "Role") == M.CONFIG_ROLE then
            matches[#matches + 1] = tool
        end
    end
    if #matches == 0 then return nil, "InputPrepConfig component not found" end
    if #matches > 1 then return nil, "multiple InputPrepConfig components found" end
    local schema = tonumber(metadata(matches[1], "SchemaVersion"))
    if schema ~= M.SCHEMA_VERSION then
        return nil, "unsupported InputPrepConfig schema: " .. tostring(schema)
    end
    return matches[1]
end

function M.read_targets(config, time)
    local targets = {}
    local seen = {}
    for index = 1, M.TARGET_SLOT_COUNT do
        local name, err = read(config, M.target_control(index), time)
        if err ~= nil then return nil, err end
        name = trim(name)
        if name ~= "" then
            if seen[name] then return nil, "duplicate target name: " .. name end
            seen[name] = true
            targets[#targets + 1] = { name = name, slot = index }
        end
    end
    if #targets == 0 then return nil, "at least one InputPrep target is required" end
    return targets
end

local APPLIED_CONTROLS = {
    "IP_EnableDepth", "IP_Depth",
    "IP_SourceColorSpace", "IP_SourceGamma",
    "IP_WorkingColorSpace", "IP_WorkingGamma", "IP_EnableColor",
    "IP_ResizeWidth", "IP_ResizeHeight", "IP_EnableResize",
    "IP_CropWidth", "IP_CropHeight", "IP_EnableCrop",
    "IP_EmbeddedAlpha", "IP_Status",
}

local function source_dimensions(target, time)
    local main_input = input(target, "MainInput1")
    if main_input == nil then return nil, nil, "MainInput1 is missing" end
    local ok, output = pcall(function() return main_input:GetConnectedOutput() end)
    if not ok or output == nil then return nil, nil, "MainInput1 is not connected" end
    local image_ok, image = pcall(function() return output[time] end)
    if not image_ok or image == nil then return nil, nil, "unable to render connected input" end
    local width = tonumber(image.Width)
    local height = tonumber(image.Height)
    if width == nil or height == nil or width <= 0 or height <= 0 then
        return nil, nil, "connected input has invalid dimensions"
    end
    return width, height
end

local function validate_target(comp, descriptor, shot, time)
    local target = comp:FindTool(descriptor.name)
    if target == nil then return nil, "target not found: " .. descriptor.name end
    if metadata(target, "Role") ~= M.ROLE then
        return nil, descriptor.name .. " is not an InputPrep processor"
    end
    if tonumber(metadata(target, "SchemaVersion")) ~= M.SCHEMA_VERSION then
        return nil, descriptor.name .. " has an unsupported InputPrep schema"
    end
    for _, id in ipairs(APPLIED_CONTROLS) do
        if input(target, id) == nil then
            return nil, descriptor.name .. " is missing control '" .. id .. "'"
        end
    end

    local source_width, source_height, dimension_error =
        source_dimensions(target, time)
    if dimension_error ~= nil then
        return nil, descriptor.name .. ": " .. dimension_error
    end
    local working_width = geometry.round_even(shot.workingWidth)
    local working_height = geometry.round_even(shot.workingHeight)
    local crop_width, crop_height = geometry.crop_dimensions(
        working_width, working_height, shot.cropRatio)
    local resize_width = geometry.fill_width(
        source_width, source_height, working_width, working_height)
    local color_enabled = shot.sourceColorSpace ~= shot.workingColorSpace or
        shot.sourceGamma ~= shot.workingGamma
    local resize_enabled = source_width ~= working_width or
        source_height ~= working_height
    local aspect_mismatch = source_width * working_height ~=
        source_height * working_width
    local crop_enabled = crop_width ~= working_width or
        crop_height ~= working_height or aspect_mismatch
    local embedded_alpha = tostring(shot.embeddedAlpha) == "true"
    local status = string.format(
        "%d x %d -> %d x %d | Depth: 3 | %s / %s -> %s / %s",
        working_width, working_height, crop_width, crop_height,
        shot.sourceColorSpace, shot.sourceGamma,
        shot.workingColorSpace, shot.workingGamma)

    local values = {
        IP_EnableDepth = 1,
        IP_Depth = 3,
        IP_SourceColorSpace = shot.sourceColorSpace,
        IP_SourceGamma = shot.sourceGamma,
        IP_WorkingColorSpace = shot.workingColorSpace,
        IP_WorkingGamma = shot.workingGamma,
        IP_EnableColor = color_enabled and 1 or 0,
        IP_ResizeWidth = resize_width,
        IP_ResizeHeight = working_height,
        IP_EnableResize = resize_enabled and 1 or 0,
        IP_CropWidth = crop_width,
        IP_CropHeight = crop_height,
        IP_EnableCrop = crop_enabled and 1 or 0,
        IP_EmbeddedAlpha = embedded_alpha and 1 or 0,
        IP_Status = status,
    }
    return { tool = target, name = descriptor.name, values = values }
end

local function comparable(value)
    if type(value) == "number" then return value end
    return tostring(value)
end

function M.prepare(comp, config, time)
    local targets, err = M.read_targets(config, time)
    if err ~= nil then return nil, err end
    local shot_config
    shot_config, err = shot_apply.find_config(comp)
    if shot_config == nil then return nil, err end
    local shot
    shot, err = shot_apply.read_config(shot_config, time)
    if shot == nil then return nil, err end
    if tonumber(shot.workingWidth) == nil or tonumber(shot.workingHeight) == nil or
        shot.workingWidth <= 0 or shot.workingHeight <= 0 then
        return nil, "Working Resolution must be positive"
    end
    if tonumber(shot.cropRatio) == nil or tonumber(shot.cropRatio) <= 0 then
        return nil, "Crop Ratio must be positive"
    end

    local prepared = {}
    for _, descriptor in ipairs(targets) do
        local item
        item, err = validate_target(comp, descriptor, shot, time)
        if item == nil then return nil, err end
        prepared[#prepared + 1] = item
    end
    return prepared
end

function M.apply_prepared(comp, prepared, time)
    local snapshots = {}
    for _, item in ipairs(prepared) do
        local saved = {}
        for _, id in ipairs(APPLIED_CONTROLS) do
            local value, err = read(item.tool, id, time)
            if err ~= nil then return false, err end
            saved[id] = value
        end
        snapshots[#snapshots + 1] = { tool = item.tool, values = saved }
    end

    comp:StartUndo("Apply InputPrep configuration")
    comp:Lock()
    local ok, failure = pcall(function()
        for _, item in ipairs(prepared) do
            for _, id in ipairs(APPLIED_CONTROLS) do
                local wrote, err = write(item.tool, id, item.values[id], time)
                if not wrote then error(item.name .. ": " .. tostring(err)) end
            end
        end
        for _, item in ipairs(prepared) do
            for _, id in ipairs(APPLIED_CONTROLS) do
                local actual, err = read(item.tool, id, time)
                if err ~= nil then error(item.name .. ": " .. err) end
                if comparable(actual) ~= comparable(item.values[id]) then
                    error(string.format("%s: verification failed for %s (expected %s, got %s)",
                        item.name, id, comparable(item.values[id]), comparable(actual)))
                end
            end
        end
    end)
    if not ok then
        for _, snapshot in ipairs(snapshots) do
            for _, id in ipairs(APPLIED_CONTROLS) do
                pcall(function() snapshot.tool[id][time] = snapshot.values[id] end)
            end
        end
    end
    comp:Unlock()
    comp:EndUndo(ok)
    if not ok then return false, tostring(failure) end
    return true
end

function M.run(comp_override)
    local comp = comp_override or rawget(_G, "comp")
    local config, err = M.find_config(comp)
    if config == nil then
        log("ERROR: " .. tostring(err) .. ". Nothing was changed.")
        return false, err
    end
    local time = comp.CurrentTime
    local prepared
    prepared, err = M.prepare(comp, config, time)
    if prepared == nil then
        set_status(config, "ERROR: " .. tostring(err) .. ". Nothing was changed.", time)
        log("ERROR: " .. tostring(err) .. ". Nothing was changed.")
        return false, err
    end
    local ok
    ok, err = M.apply_prepared(comp, prepared, time)
    if not ok then
        set_status(config, "ERROR: " .. tostring(err) .. ". Nothing was changed.", time)
        log("ERROR: " .. tostring(err) .. ". Nothing was changed.")
        return false, err
    end
    local message = string.format("Applied %d InputPrep target(s).", #prepared)
    set_status(config, message, time)
    log(message)
    return true
end

return M
