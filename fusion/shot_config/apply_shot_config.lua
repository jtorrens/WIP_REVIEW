-- ShotConfig v0.1 explicit target resolver and transactional applicator.
-- This file is loaded by build_shot_config.lua and can also be executed from
-- Fusion Console. It intentionally discovers only the ShotConfig component;
-- Loader and Saver targets are always resolved by their configured names.

local M = {}

M.ROLE = "ShotConfig"
M.SCHEMA_VERSION = 1
M.TARGET_SLOT_COUNT = 5

M.DATA = {
    color_space_ids = "ColorSpaceIds",
    gamma_ids = "GammaIds",
}

local CONTROL = {
    show = "SC_Show",
    episode = "SC_Episode",
    shot = "SC_Shot",
    version = "SC_Version",
    root = "SC_RootPathMap",
    working_resolution = "SC_WorkingResolution",
    crop_ratio = "SC_CropRatio",
    review_resolution = "SC_ReviewResolution",
    source_color_space_choice = "SC_SourceColorSpaceChoice",
    source_gamma_choice = "SC_SourceGammaChoice",
    working_color_space_choice = "SC_WorkingColorSpaceChoice",
    working_gamma_choice = "SC_WorkingGammaChoice",
    embedded_alpha = "SC_EmbeddedAlpha",
    status = "SC_Status",
}
M.CONTROL = CONTROL

function M.target_controls(kind, index)
    if kind ~= "Loader" and kind ~= "Saver" then
        error("target kind must be Loader or Saver")
    end
    if type(index) ~= "number" or index < 1 or index > M.TARGET_SLOT_COUNT then
        error("target slot index is outside configured range")
    end
    local prefix = "SC_" .. kind .. "Target" .. tostring(index)
    return prefix .. "Node", prefix .. "Template", prefix .. "Resolved"
end

local function trim(value)
    return tostring(value or ""):match("^%s*(.-)%s*$")
end

local function log(message)
    print("[ShotConfig] " .. message)
end

local function get_input(tool, name)
    if tool == nil then
        return nil
    end
    local ok, input = pcall(function()
        return tool:FindInput(name)
    end)
    if ok and input ~= nil then
        return input
    end
    ok, input = pcall(function()
        return tool[name]
    end)
    if ok then
        return input
    end
    return nil
end

local function read_input(tool, name, time)
    local input = get_input(tool, name)
    if input == nil then
        return nil, "control '" .. name .. "' is missing"
    end
    local ok, value = pcall(function()
        return input[time]
    end)
    if not ok then
        return nil, "unable to read control '" .. name .. "': " .. tostring(value)
    end
    return value
end

local function write_input(tool, name, value, time)
    local input = get_input(tool, name)
    if input == nil then
        return false, "control '" .. name .. "' is missing"
    end
    local ok, result = pcall(function()
        input[time] = value
        return true
    end)
    if not ok then
        return false, tostring(result)
    end
    if result == false then
        return false, "Fusion rejected the value"
    end
    return true
end

local function set_status(config, message, time)
    local ok = write_input(config, CONTROL.status, message, time)
    if not ok then
        log("WARNING: unable to update status control")
    end
end

local function metadata(tool, key)
    local ok, value = pcall(function()
        return tool:GetData("ShotConfig." .. key)
    end)
    if ok then
        return value
    end
    return nil
end

function M.find_config(comp)
    if comp == nil then
        return nil, "no active composition"
    end
    local matches = {}
    local ok, tools = pcall(function()
        return comp:GetToolList(false)
    end)
    if not ok or tools == nil then
        return nil, "unable to inspect composition tools"
    end
    for _, tool in pairs(tools) do
        if metadata(tool, "Role") == M.ROLE then
            matches[#matches + 1] = tool
        end
    end
    if #matches == 0 then
        return nil, "ShotConfig component not found"
    end
    if #matches > 1 then
        return nil, "multiple ShotConfig components found"
    end
    local schema = tonumber(metadata(matches[1], "SchemaVersion"))
    if schema ~= M.SCHEMA_VERSION then
        return nil, "unsupported ShotConfig schema: " .. tostring(schema)
    end
    return matches[1]
end

local function point_components(value, label)
    if type(value) ~= "table" then
        return nil, nil, label .. " is not a two-value control"
    end
    local x = tonumber(value[1] or value.X)
    local y = tonumber(value[2] or value.Y)
    if x == nil or y == nil then
        return nil, nil, label .. " must contain two numeric values"
    end
    return x, y
end

local function split_lines(value)
    local result = {}
    local normalized = tostring(value or ""):gsub("\r\n", "\n"):gsub("\r", "\n")
    for line in (normalized .. "\n"):gmatch("(.-)\n") do
        local id = trim(line)
        if id ~= "" then result[#result + 1] = id end
    end
    return result
end

local function read_enum(config, choice_control, ids_key, label, time)
    local choice, err = read_input(config, choice_control, time)
    if err ~= nil then return nil, err end
    local serialized_ids = metadata(config, ids_key)
    if type(serialized_ids) ~= "string" or serialized_ids == "" then
        return nil, label .. " enum metadata is missing"
    end
    local ids = split_lines(serialized_ids)
    local index = tonumber(choice)
    if index == nil or index % 1 ~= 0 then
        return nil, label .. " selection is invalid"
    end
    local id = ids[index + 1]
    if id == nil then
        return nil, label .. " selection is outside the embedded enum"
    end
    return id
end

local function read_target_slots(config, kind, time)
    local targets = {}
    for index = 1, M.TARGET_SLOT_COUNT do
        local node_control, template_control = M.target_controls(kind, index)
        local node_name, err = read_input(config, node_control, time)
        if err ~= nil then return nil, err end
        local template
        template, err = read_input(config, template_control, time)
        if err ~= nil then return nil, err end
        node_name = trim(node_name)
        template = trim(template)
        if node_name ~= "" then
            if template == "" then
                return nil, string.format(
                    "%s target slot %d requires a Path Template",
                    kind, index)
            end
            targets[#targets + 1] = {
                kind = kind,
                nodeName = node_name,
                template = template,
                slot = index,
            }
        end
    end
    return targets
end

function M.read_config(config, time)
    local values = {}
    local scalar_fields = {
        { "show", CONTROL.show },
        { "episode", CONTROL.episode },
        { "shot", CONTROL.shot },
        { "version", CONTROL.version },
        { "root", CONTROL.root },
        { "cropRatio", CONTROL.crop_ratio },
        { "embeddedAlpha", CONTROL.embedded_alpha },
    }
    for _, field in ipairs(scalar_fields) do
        local value, err = read_input(config, field[2], time)
        if err ~= nil then
            return nil, err
        end
        values[field[1]] = value
    end

    local working, err = read_input(config, CONTROL.working_resolution, time)
    if err ~= nil then return nil, err end
    values.workingWidth, values.workingHeight, err =
        point_components(working, "Working Resolution")
    if err ~= nil then return nil, err end

    local review
    review, err = read_input(config, CONTROL.review_resolution, time)
    if err ~= nil then return nil, err end
    values.reviewWidth, values.reviewHeight, err =
        point_components(review, "Review Resolution")
    if err ~= nil then return nil, err end

    values.cropX = tonumber(values.cropRatio)
    values.cropY = 1
    values.embeddedAlpha = tonumber(values.embeddedAlpha) == 1 and "true" or "false"

    values.sourceColorSpace, err = read_enum(config,
        CONTROL.source_color_space_choice, M.DATA.color_space_ids,
        "Source Color Space", time)
    if err ~= nil then return nil, err end
    values.sourceGamma, err = read_enum(config,
        CONTROL.source_gamma_choice, M.DATA.gamma_ids,
        "Source Gamma", time)
    if err ~= nil then return nil, err end
    values.workingColorSpace, err = read_enum(config,
        CONTROL.working_color_space_choice, M.DATA.color_space_ids,
        "Working Color Space", time)
    if err ~= nil then return nil, err end
    values.workingGamma, err = read_enum(config,
        CONTROL.working_gamma_choice, M.DATA.gamma_ids,
        "Working Gamma", time)
    if err ~= nil then return nil, err end

    values.loaderTargets, err = read_target_slots(config, "Loader", time)
    if err ~= nil then return nil, err end
    values.saverTargets, err = read_target_slots(config, "Saver", time)
    if err ~= nil then return nil, err end

    for _, key in ipairs({ "show", "episode", "shot", "version", "root" }) do
        values[key] = trim(values[key])
    end
    values.root = values.root:gsub("/+$", "")
    return values
end

local function token_values(values)
    return {
        root = values.root,
        show = values.show,
        episode = values.episode,
        shot = values.shot,
        version = values.version,
        workingWidth = tostring(math.floor(values.workingWidth + 0.5)),
        workingHeight = tostring(math.floor(values.workingHeight + 0.5)),
        cropX = tostring(values.cropX),
        cropY = tostring(values.cropY),
        reviewWidth = tostring(math.floor(values.reviewWidth + 0.5)),
        reviewHeight = tostring(math.floor(values.reviewHeight + 0.5)),
        sourceColorSpace = values.sourceColorSpace,
        sourceGamma = values.sourceGamma,
        workingColorSpace = values.workingColorSpace,
        workingGamma = values.workingGamma,
        embeddedAlpha = values.embeddedAlpha,
    }
end

function M.resolve_template(template, values)
    local tokens = token_values(values)
    local unknown = {}
    local resolved = tostring(template):gsub("{([^{}]+)}", function(name)
        local value = tokens[name]
        if value == nil then
            unknown[#unknown + 1] = name
            return "{" .. name .. "}"
        end
        return value
    end)
    if #unknown > 0 then
        return nil, "unknown token(s): {" .. table.concat(unknown, "}, {") .. "}"
    end
    if resolved:find("[{}]") then
        return nil, "malformed token syntax"
    end
    return resolved
end

local function reg_id(tool)
    local ok, attrs = pcall(function() return tool:GetAttrs() end)
    if not ok or attrs == nil then return nil end
    return attrs.TOOLS_RegID
end

local function find_exact_tool(comp, name)
    local ok, tool = pcall(function() return comp:FindTool(name) end)
    if ok then return tool end
    return nil
end

function M.prepare(comp, config, time)
    local values, err = M.read_config(config, time)
    if values == nil then return nil, err end
    if values.root == "" then
        return nil, "Root Path Map is empty"
    end

    local targets = {}
    for _, target in ipairs(values.loaderTargets) do targets[#targets + 1] = target end
    for _, target in ipairs(values.saverTargets) do targets[#targets + 1] = target end
    if #targets == 0 then
        return nil, "no Loader or Saver targets are configured"
    end

    log("Validating " .. #targets .. " targets")
    local seen = {}
    for _, target in ipairs(targets) do
        if seen[target.nodeName] then
            return nil, "target '" .. target.nodeName .. "' is declared more than once"
        end
        seen[target.nodeName] = true

        target.resolved, err = M.resolve_template(target.template, values)
        if target.resolved == nil then
            return nil, string.format("%s target '%s': %s",
                target.kind, target.nodeName, err)
        end
        target.tool = find_exact_tool(comp, target.nodeName)
        if target.tool == nil then
            return nil, "target '" .. target.nodeName .. "' not found"
        end
        local actual_type = reg_id(target.tool)
        if actual_type ~= target.kind then
            return nil, string.format("target '%s' must be a %s (found %s)",
                target.nodeName, target.kind, tostring(actual_type))
        end
        target.input = get_input(target.tool, "Clip")
        if target.input == nil then
            return nil, "target '" .. target.nodeName .. "' has no Clip input"
        end
        local ok, previous = pcall(function() return target.input[time] end)
        if not ok then
            return nil, "unable to read Clip from target '" .. target.nodeName .. "'"
        end
        target.previous = previous
        log(target.kind .. " " .. target.nodeName .. " OK")
    end
    return targets
end

local function rollback(applied, time)
    local failures = {}
    for index = #applied, 1, -1 do
        local target = applied[index]
        local ok, result = pcall(function()
            target.input[time] = target.previous
            return true
        end)
        if not ok or result == false then
            failures[#failures + 1] = target.nodeName
        end
    end
    return failures
end

function M.apply_prepared(targets, time)
    local applied = {}
    log("Applying...")
    for _, target in ipairs(targets) do
        local ok, result = pcall(function()
            target.input[time] = target.resolved
            return true
        end)
        if not ok or result == false then
            -- Include the failing target: a host setter could mutate its input
            -- before reporting failure.
            applied[#applied + 1] = target
            local rollback_failures = rollback(applied, time)
            local message = "write failed for target '" .. target.nodeName .. "'"
            if not ok then message = message .. ": " .. tostring(result) end
            if #rollback_failures > 0 then
                message = message .. "; rollback also failed for: " ..
                    table.concat(rollback_failures, ", ")
            else
                message = message .. "; previous writes were rolled back"
            end
            return false, message
        end
        applied[#applied + 1] = target
    end
    return true
end

function M.run(comp_override)
    local active_comp = comp_override or rawget(_G, "comp")
    local time = active_comp and active_comp.CurrentTime or 0
    local config, err = M.find_config(active_comp)
    if config == nil then
        log("ERROR: " .. err .. ". Nothing was changed.")
        return false, err
    end

    local targets
    targets, err = M.prepare(active_comp, config, time)
    if targets == nil then
        local message = "ERROR: " .. err .. ". Nothing was changed."
        log(message)
        set_status(config, message, time)
        return false, err
    end

    local undo_started = false
    local ok = pcall(function()
        active_comp:StartUndo("Apply ShotConfig")
        undo_started = true
    end)
    if not ok then undo_started = false end

    local applied
    applied, err = M.apply_prepared(targets, time)
    if undo_started then
        pcall(function() active_comp:EndUndo(applied) end)
    end
    if not applied then
        local message = "ERROR: " .. err
        log(message)
        set_status(config, message, time)
        return false, err
    end

    local message = "OK: Updated " .. #targets .. " targets"
    set_status(config, message, time)
    log("Updated " .. #targets .. " targets")
    return true, targets
end

-- Resolves and updates only Saver paths. Intended for a version bump after the
-- managed graph is already in place; it never writes Loader clips or topology.
function M.run_savers(comp_override)
    local active_comp = comp_override or rawget(_G, "comp")
    local time = active_comp and active_comp.CurrentTime or 0
    local config, err = M.find_config(active_comp)
    if config == nil then return false, err end
    local values
    values, err = M.read_config(config, time)
    if values == nil then return false, err end
    if values.root == "" then return false, "Root Path Map is empty" end
    if #values.saverTargets == 0 then return false, "no Saver targets are configured" end

    local targets = {}
    local seen = {}
    for _, target in ipairs(values.saverTargets) do
        if seen[target.nodeName] then return false, "duplicate Saver target: " .. target.nodeName end
        seen[target.nodeName] = true
        target.resolved, err = M.resolve_template(target.template, values)
        if target.resolved == nil then return false, err end
        target.tool = find_exact_tool(active_comp, target.nodeName)
        if target.tool == nil then return false, "Saver target not found: " .. target.nodeName end
        if reg_id(target.tool) ~= "Saver" then return false, target.nodeName .. " is not a Saver" end
        target.input = get_input(target.tool, "Clip")
        if target.input == nil then return false, target.nodeName .. " has no Clip input" end
        target.previous = target.input[time]
        targets[#targets + 1] = target
    end
    local applied, apply_err = M.apply_prepared(targets, time)
    if not applied then
        set_status(config, "ERROR: " .. tostring(apply_err), time)
        return false, apply_err
    end
    local message = "OK: Updated " .. #targets .. " Saver path(s)"
    set_status(config, message, time)
    log(message)
    return true, targets
end

return M
