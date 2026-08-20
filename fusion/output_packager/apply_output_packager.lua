-- Applies ShotConfig and explicit OutputPackager/Saver pairs transactionally.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local shot_apply = dofile(script_directory() .. "../shot_config/apply_shot_config.lua")
local M = {}

M.ROLE = "OutputPackager"
M.CONFIG_ROLE = "OutputPackagerConfig"
M.SCHEMA_VERSION = 1
M.TARGET_SLOT_COUNT = 5
M.CONTROL = {
    apply = "OPC_Apply",
    status = "OPC_Status",
}

local FIELD_SUFFIX = {
    packager = "Packager",
    saver = "Saver",
    enabled = "Enabled",
    review = "ReviewRaster",
    wip = "WIP",
}

function M.target_control(index, field)
    if type(index) ~= "number" or index < 1 or index > M.TARGET_SLOT_COUNT then
        error("target slot index is outside configured range")
    end
    local suffix = FIELD_SUFFIX[field]
    if suffix == nil then error("unknown target field: " .. tostring(field)) end
    return "OPC_Target" .. tostring(index) .. suffix
end

local APPLIED_CONTROLS = {
    "OP_EnableReviewRaster",
    "OP_EnableWIP",
    "OP_ReviewWidth",
    "OP_ReviewHeight",
    "OP_CropRatio",
    "OP_Status",
}

local function trim(value)
    return tostring(value or ""):match("^%s*(.-)%s*$")
end

local function log(message)
    print("[OutputPackager] " .. message)
end

local function metadata(tool, key)
    local ok, value = pcall(function()
        return tool:GetData("OutputPackager." .. key)
    end)
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
    if not ok then log("WARNING: unable to update OutputPackagerConfig status") end
end

function M.find_config(comp)
    if comp == nil then return nil, "no active composition" end
    local matches = {}
    for _, tool in pairs(comp:GetToolList(false) or {}) do
        if metadata(tool, "Role") == M.CONFIG_ROLE then
            matches[#matches + 1] = tool
        end
    end
    if #matches == 0 then return nil, "OutputPackagerConfig component not found" end
    if #matches > 1 then return nil, "multiple OutputPackagerConfig components found" end
    local schema = tonumber(metadata(matches[1], "SchemaVersion"))
    if schema ~= M.SCHEMA_VERSION then
        return nil, "unsupported OutputPackagerConfig schema: " .. tostring(schema)
    end
    return matches[1]
end

function M.read_targets(config, time)
    local targets = {}
    local packagers = {}
    local savers = {}
    for index = 1, M.TARGET_SLOT_COUNT do
        local row = { slot = index }
        local err
        row.packager, err = read(config, M.target_control(index, "packager"), time)
        if err ~= nil then return nil, err end
        row.saver, err = read(config, M.target_control(index, "saver"), time)
        if err ~= nil then return nil, err end
        row.enabled, err = read(config, M.target_control(index, "enabled"), time)
        if err ~= nil then return nil, err end
        row.review, err = read(config, M.target_control(index, "review"), time)
        if err ~= nil then return nil, err end
        row.wip, err = read(config, M.target_control(index, "wip"), time)
        if err ~= nil then return nil, err end
        row.packager = trim(row.packager)
        row.saver = trim(row.saver)
        row.enabled = tonumber(row.enabled) == 1
        row.review = tonumber(row.review) == 1
        row.wip = tonumber(row.wip) == 1

        if row.packager == "" and row.saver == "" then
            -- Empty rows are ignored.
        elseif row.packager == "" or row.saver == "" then
            return nil, "target " .. index .. " must define both Packager and Saver"
        else
            if packagers[row.packager] then
                return nil, "duplicate OutputPackager target: " .. row.packager
            end
            if savers[row.saver] then return nil, "duplicate Saver target: " .. row.saver end
            if row.wip and not row.review then
                return nil, "target " .. index .. ": WIP Review requires Review Raster"
            end
            packagers[row.packager] = true
            savers[row.saver] = true
            targets[#targets + 1] = row
        end
    end
    if #targets == 0 then return nil, "at least one OutputPackager target is required" end
    return targets
end

local function first_output(tool)
    for _, output in pairs(tool:GetOutputList() or {}) do return output end
    return nil
end

local function validate_connection(packager, saver)
    local saver_input = input(saver, "Input")
    if saver_input == nil then return false, "Saver input is missing" end
    local expected = first_output(packager)
    if expected == nil then return false, "OutputPackager has no output" end
    local ok, consumers = pcall(function() return expected:GetConnectedInputs() end)
    if not ok or type(consumers) ~= "table" then
        return false, "unable to inspect OutputPackager consumers"
    end
    for _, consumer in pairs(consumers) do
        if consumer == saver_input or tostring(consumer) == tostring(saver_input) then
            return true
        end
    end
    return false, "Saver is connected to a different source"
end

local function validate_target(comp, descriptor, shot)
    local packager = comp:FindTool(descriptor.packager)
    if packager == nil then
        return nil, "OutputPackager not found: " .. descriptor.packager
    end
    if metadata(packager, "Role") ~= M.ROLE then
        return nil, descriptor.packager .. " is not an OutputPackager"
    end
    if tonumber(metadata(packager, "SchemaVersion")) ~= M.SCHEMA_VERSION then
        return nil, descriptor.packager .. " has an unsupported OutputPackager schema"
    end
    for _, id in ipairs(APPLIED_CONTROLS) do
        if input(packager, id) == nil then
            return nil, descriptor.packager .. " is missing control '" .. id .. "'"
        end
    end

    local saver = comp:FindTool(descriptor.saver)
    if saver == nil then return nil, "Saver not found: " .. descriptor.saver end
    local saver_attrs = saver:GetAttrs() or {}
    if saver_attrs.TOOLS_RegID ~= "Saver" then
        return nil, descriptor.saver .. " is not a Saver"
    end
    local connected, connection_error = validate_connection(packager, saver)
    if not connected then
        return nil, descriptor.saver .. ": " .. connection_error
    end

    local review_width = math.floor(tonumber(shot.reviewWidth) + 0.5)
    local review_height = math.floor(tonumber(shot.reviewHeight) + 0.5)
    local crop_ratio = tonumber(shot.cropRatio)
    local status = string.format("Review: %d x %d | Crop: %.4g | WIP: %s",
        review_width, review_height, crop_ratio, descriptor.wip and "on" or "off")
    return {
        name = descriptor.packager,
        tool = packager,
        saver = saver,
        saverName = descriptor.saver,
        saverPassThrough = not descriptor.enabled,
        values = {
            OP_EnableReviewRaster = descriptor.review and 1 or 0,
            OP_EnableWIP = descriptor.wip and 1 or 0,
            OP_ReviewWidth = review_width,
            OP_ReviewHeight = review_height,
            OP_CropRatio = crop_ratio,
            OP_Status = status,
        },
    }
end

function M.prepare(comp, config, time)
    local targets, err = M.read_targets(config, time)
    if targets == nil then return nil, err end
    local shot_config
    shot_config, err = shot_apply.find_config(comp)
    if shot_config == nil then return nil, err end
    local shot
    shot, err = shot_apply.read_config(shot_config, time)
    if shot == nil then return nil, err end
    if tonumber(shot.reviewWidth) == nil or tonumber(shot.reviewHeight) == nil or
        shot.reviewWidth <= 0 or shot.reviewHeight <= 0 then
        return nil, "Review Resolution must be positive"
    end
    if tonumber(shot.cropRatio) == nil or tonumber(shot.cropRatio) <= 0 then
        return nil, "Crop Ratio must be positive"
    end

    local prepared = {}
    for _, descriptor in ipairs(targets) do
        local item
        item, err = validate_target(comp, descriptor, shot)
        if item == nil then return nil, err end
        prepared[#prepared + 1] = item
    end
    return prepared
end

local function comparable(value)
    if type(value) == "number" then return value end
    return tostring(value)
end

function M.apply_prepared(comp, prepared, time)
    local snapshots = {}
    for _, item in ipairs(prepared) do
        local values = {}
        for _, id in ipairs(APPLIED_CONTROLS) do
            local value, err = read(item.tool, id, time)
            if err ~= nil then return false, err end
            values[id] = value
        end
        local attrs = item.saver:GetAttrs() or {}
        snapshots[#snapshots + 1] = {
            tool = item.tool,
            values = values,
            saver = item.saver,
            saverPassThrough = attrs.TOOLB_PassThrough == true,
        }
    end

    comp:StartUndo("Apply OutputPackager configuration")
    comp:Lock()
    local ok, failure = pcall(function()
        for _, item in ipairs(prepared) do
            for _, id in ipairs(APPLIED_CONTROLS) do
                local wrote, err = write(item.tool, id, item.values[id], time)
                if not wrote then error(item.name .. ": " .. tostring(err)) end
            end
            item.saver:SetAttrs({ TOOLB_PassThrough = item.saverPassThrough })
        end
        for _, item in ipairs(prepared) do
            for _, id in ipairs(APPLIED_CONTROLS) do
                local actual, err = read(item.tool, id, time)
                if err ~= nil then error(item.name .. ": " .. err) end
                if comparable(actual) ~= comparable(item.values[id]) then
                    error(string.format(
                        "%s: verification failed for %s (expected %s, got %s)",
                        item.name, id, comparable(item.values[id]), comparable(actual)))
                end
            end
            local attrs = item.saver:GetAttrs() or {}
            if (attrs.TOOLB_PassThrough == true) ~= item.saverPassThrough then
                error(item.saverName .. ": Saver state verification failed")
            end
        end
    end)
    if not ok then
        for _, snapshot in ipairs(snapshots) do
            for _, id in ipairs(APPLIED_CONTROLS) do
                pcall(function() snapshot.tool[id][time] = snapshot.values[id] end)
            end
            pcall(function()
                snapshot.saver:SetAttrs({
                    TOOLB_PassThrough = snapshot.saverPassThrough,
                })
            end)
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
    local message = string.format("Applied %d output package(s).", #prepared)
    set_status(config, message, time)
    log(message)
    return true
end

return M
