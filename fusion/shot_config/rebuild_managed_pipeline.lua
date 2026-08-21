-- Rebuilds only FOQNPipeline-tagged nodes from G_ShotConfig targets.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local SCRIPT_DIR = script_directory()
local shot_apply = dofile(SCRIPT_DIR .. "apply_shot_config.lua")
local input_builder_path = SCRIPT_DIR .. "../input_prep/build_input_prep.lua"
local output_builder_path = SCRIPT_DIR .. "../output_packager/build_output_packager.lua"
local M = {}

local TAG = "FOQNPipeline."
local function tagged(tool)
    return tool:GetData(TAG .. "Managed") == true
end
local function mark(tool, role)
    tool:SetData(TAG .. "Managed", true)
    tool:SetData(TAG .. "Role", role)
    tool:SetData(TAG .. "SchemaVersion", 1)
end
local function first_output(tool)
    for _, output in pairs(tool:GetOutputList() or {}) do return output end
    error((tool:GetAttrs().TOOLS_Name or "tool") .. " has no output")
end
local function display_name(name)
    local value = tostring(name or "")
    if value == value:upper() then return value:sub(1, 1) .. value:sub(2):lower() end
    return value
end
local function input_prep_name(loader_name)
    return "L_" .. display_name(loader_name)
end
local function output_packager_name(saver_name)
    return "S_" .. display_name(saver_name)
end
local function add_tool(comp, reg_id, name, x, y, role)
    local tool = comp:AddTool(reg_id, x, y)
    if tool == nil then error("could not create " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    mark(tool, role)
    return tool
end
local function managed_positions(comp)
    local saved = {}
    local flow = comp.CurrentFrame and comp.CurrentFrame.FlowView
    if flow == nil then return saved end
    for _, tool in pairs(comp:GetToolList(false) or {}) do
        if tagged(tool) then
            local attrs = tool:GetAttrs() or {}
            local ok, x, y = pcall(function() return flow:GetPos(tool) end)
            if ok and x ~= nil and y ~= nil then saved[attrs.TOOLS_Name] = { x, y } end
        end
    end
    return saved
end
local function place(comp, tool, positions, had_managed, fallback_x, fallback_y)
    local flow = comp.CurrentFrame and comp.CurrentFrame.FlowView
    if flow == nil then return end
    local name = (tool:GetAttrs() or {}).TOOLS_Name
    local position = positions[name]
    if position ~= nil then
        pcall(function() flow:SetPos(tool, position[1], position[2]) end)
    elseif had_managed then
        pcall(function() flow:SetPos(tool, fallback_x, fallback_y) end)
    end
end
local function validate(values)
    if #values.loaderTargets == 0 then return "configure at least one Loader target" end
    if #values.saverTargets == 0 then return "configure at least one Saver target" end
    local seen = {}
    for _, kind in ipairs({ values.loaderTargets, values.saverTargets }) do
        for _, item in ipairs(kind) do
            if item.nodeName == "" or seen[item.nodeName] then
                return "Loader and Saver target names must be unique and non-empty"
            end
            seen[item.nodeName] = true
        end
    end
end

function M.run(comp_override)
    local comp = comp_override or rawget(_G, "comp")
    local time = comp and comp.CurrentTime or 0
    local config, err = shot_apply.find_config(comp)
    if config == nil then error(err or "G_ShotConfig was not found") end
    local values
    values, err = shot_apply.read_config(config, time)
    if values == nil then error(err) end
    err = validate(values)
    if err ~= nil then
        config[shot_apply.CONTROL.status][time] = "ERROR: " .. err
        return false, err
    end

    comp:StartUndo("Rebuild FOQN managed pipeline")
    comp:Lock()
    local ok, failure = pcall(function()
        local positions = managed_positions(comp)
        local had_managed = next(positions) ~= nil
        for _, tool in pairs(comp:GetToolList(false) or {}) do
            if tagged(tool) then
                local deleted = tool:Delete()
                if deleted == false then error("could not remove managed node") end
            end
        end

        local loaders = {}
        for index, target in ipairs(values.loaderTargets) do
            loaders[index] = add_tool(comp, "Loader", target.nodeName, -6, index * 2, "Loader")
            place(comp, loaders[index], positions, had_managed, -10, 8 + index * 2)
        end
        local input_builder = dofile(input_builder_path)
        local input_prep = input_builder.last_group
        input_prep:SetAttrs({ TOOLS_Name = input_prep_name(values.loaderTargets[1].nodeName) })
        mark(input_prep, "InputPrep")
        place(comp, input_prep, positions, had_managed, -7, 8)
        input_prep.MainInput1 = loaders[1].Output

        local output_builder = dofile(output_builder_path)
        local first_packager = output_builder.last_group
        for index, target in ipairs(values.saverTargets) do
            local packager = index == 1 and first_packager or output_builder.run(comp)
            packager:SetAttrs({ TOOLS_Name = output_packager_name(target.nodeName) })
            mark(packager, "OutputPackager")
            place(comp, packager, positions, had_managed, 0, 8 + index * 2)
            packager.MainInput1 = first_output(input_prep)
            local saver = add_tool(comp, "Saver", target.nodeName, 3, index * 2, "Saver")
            place(comp, saver, positions, had_managed, 5, 8 + index * 2)
            saver.Input:ConnectTo(first_output(packager))
            if target.template:upper():find("WIP", 1, true) then packager.OP_EnableWIP[time] = 1 end
        end
    end)
    comp:Unlock()
    comp:EndUndo(ok)
    if not ok then
        config[shot_apply.CONTROL.status][time] = "ERROR: rebuild failed: " .. tostring(failure)
        return false, failure
    end
    local applied, apply_err = shot_apply.run(comp)
    if not applied then return false, apply_err end
    config[shot_apply.CONTROL.status][time] = "OK: rebuilt managed pipeline"
    return true
end

return M
