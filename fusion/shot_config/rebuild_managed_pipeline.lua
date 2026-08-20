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
local function add_tool(comp, reg_id, name, x, y, role)
    local tool = comp:AddTool(reg_id, x, y)
    if tool == nil then error("could not create " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    mark(tool, role)
    return tool
end
local function has_name(list, name)
    for _, item in ipairs(list) do if item.nodeName == name then return true end end
    return false
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
        for _, tool in pairs(comp:GetToolList(false) or {}) do
            if tagged(tool) then
                local deleted = tool:Delete()
                if deleted == false then error("could not remove managed node") end
            end
        end

        local loaders = {}
        for index, target in ipairs(values.loaderTargets) do
            loaders[index] = add_tool(comp, "Loader", target.nodeName, -6, index * 2, "Loader")
        end
        local input_builder = dofile(input_builder_path)
        local input_prep = input_builder.last_group
        input_prep:SetAttrs({ TOOLS_Name = "L_InputPrep_1" })
        mark(input_prep, "InputPrep")
        input_prep.MainInput1 = loaders[1].Output

        local output_builder = dofile(output_builder_path)
        local first_packager = output_builder.last_group
        for index, target in ipairs(values.saverTargets) do
            local packager = index == 1 and first_packager or output_builder.run(comp)
            packager:SetAttrs({ TOOLS_Name = "S_OutputPackager_" .. tostring(index) })
            mark(packager, "OutputPackager")
            packager.MainInput1 = first_output(input_prep)
            local saver = add_tool(comp, "Saver", target.nodeName, 3, index * 2, "Saver")
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
