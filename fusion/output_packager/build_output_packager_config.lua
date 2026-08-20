-- Builds or rebuilds the explicit OutputPackager/Saver target registry.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local SCRIPT_DIR = script_directory()
local APPLY_PATH = SCRIPT_DIR .. "apply_output_packager.lua"
local apply = dofile(APPLY_PATH)
local M = {}

local function text_control(id, label, default, read_only)
    return string.format([[
        %s = {
            LINKS_Name = %q,
            LINKID_DataType = "Text",
            INPID_InputControl = "TextEditControl",
            INP_Default = %q,
            TEC_Lines = 1,
            TEC_Wrap = false,
            TEC_ReadOnly = %s,
            ICS_ControlPage = "Targets",
        },]], id, label, default, read_only and "true" or "false")
end

local function checkbox_control(id, label, default)
    return string.format([[
        %s = {
            LINKS_Name = %q,
            LINKID_DataType = "Number",
            INPID_InputControl = "CheckboxControl",
            INP_Default = %d,
            CBC_TriState = false,
            ICS_ControlPage = "Targets",
        },]], id, label, default and 1 or 0)
end

local function label_control(id, label)
    return string.format([[
        %s = {
            LINKS_Name = %q,
            LINKID_DataType = "Text",
            INPID_InputControl = "LabelControl",
            INP_External = false,
            INP_Passive = true,
            ICS_ControlPage = "Targets",
        },]], id, label)
end

local function button_control()
    local execute = string.format("local m = dofile(%q); m.run(comp)", APPLY_PATH)
    return string.format([[
        %s = {
            LINKS_Name = "Apply / Update",
            LINKID_DataType = "Number",
            INPID_InputControl = "ButtonControl",
            BTNCS_Execute = %q,
            ICS_ControlPage = "Targets",
        },]], apply.CONTROL.apply, execute)
end

local function controls()
    local rows = {
        label_control("OPC_TargetSection", "Output Packages"),
        label_control("OPC_TargetHelp",
            "F2 copies a node name. Empty pairs are ignored."),
    }
    for index = 1, apply.TARGET_SLOT_COUNT do
        rows[#rows + 1] = label_control("OPC_Target" .. index .. "Section",
            "Package " .. index)
        rows[#rows + 1] = text_control(apply.target_control(index, "packager"),
            "OutputPackager Node", "", false)
        rows[#rows + 1] = text_control(apply.target_control(index, "saver"),
            "Saver Node", "", false)
        rows[#rows + 1] = checkbox_control(apply.target_control(index, "enabled"),
            "Enabled", index == 1)
        rows[#rows + 1] = checkbox_control(apply.target_control(index, "review"),
            "Review Raster", true)
        rows[#rows + 1] = checkbox_control(apply.target_control(index, "wip"),
            "WIP Review", index == 1)
    end
    rows[#rows + 1] = button_control()
    rows[#rows + 1] = text_control(apply.CONTROL.status, "Status", "Ready", true)
    return table.concat(rows, "\n")
end

local function role_matches(comp)
    local matches = {}
    for _, tool in pairs(comp:GetToolList(false) or {}) do
        if tool:GetData("OutputPackager.Role") == apply.CONFIG_ROLE then
            matches[#matches + 1] = tool
        end
    end
    return matches
end

local function read_value(tool, id, time)
    if tool == nil or tool[id] == nil then return nil end
    local ok, value = pcall(function() return tool[id][time] end)
    if ok then return value end
    return nil
end

local function flow_position(comp, tool)
    if tool == nil or comp.CurrentFrame == nil then return nil, nil end
    local flow = comp.CurrentFrame.FlowView
    if flow == nil then return nil, nil end
    local ok, x, y = pcall(function() return flow:GetPos(tool) end)
    if ok then return x, y end
    return nil, nil
end

local function set_flow_position(comp, tool, x, y)
    if x == nil or y == nil or comp.CurrentFrame == nil then return end
    local flow = comp.CurrentFrame.FlowView
    if flow ~= nil then pcall(function() flow:SetPos(tool, x, y) end) end
end

local function create_group(comp)
    local name = "OutputPackagerConfigBuild_" .. tostring(os.time())
    while comp:FindTool(name) ~= nil do name = name .. "_1" end
    local source = string.format([[
    {
        Tools = ordered() {
            %s = GroupOperator {
                UserControls = ordered() {
                    %s
                },
                ViewInfo = GroupInfo { Pos = { -32768, -32768 } },
            },
        },
        ActiveTool = %q,
    }
    ]], name, controls(), name)
    local parsed = bmd.readstring(source)
    if parsed == nil then error("Fusion could not parse OutputPackagerConfig") end
    local pasted = comp:Paste(parsed)
    if pasted == false then error("Fusion could not paste OutputPackagerConfig") end
    local group = comp:FindTool(name)
    if group == nil then error("pasted OutputPackagerConfig was not found") end
    return group
end

function M.run(comp_override)
    local comp = comp_override or rawget(_G, "comp")
    if comp == nil then error("OutputPackagerConfig builder requires an active composition") end
    local matches = role_matches(comp)
    if #matches > 1 then
        error("multiple OutputPackagerConfig components found; rebuild aborted")
    end
    local previous = matches[1]
    local time = comp.CurrentTime or 0
    local saved = {}
    for index = 1, apply.TARGET_SLOT_COUNT do
        for _, field in ipairs({ "packager", "saver", "enabled", "review", "wip" }) do
            local id = apply.target_control(index, field)
            saved[id] = read_value(previous, id, time)
        end
    end
    local x, y = flow_position(comp, previous)

    comp:StartUndo("Build OutputPackagerConfig v0.1")
    comp:Lock()
    local new_config = nil
    local ok, failure = pcall(function()
        new_config = create_group(comp)
        if previous ~= nil then
            for id, value in pairs(saved) do
                if value ~= nil then new_config[id][time] = value end
            end
        end
        new_config[apply.CONTROL.status][time] = previous and
            "Ready (rebuilt; targets preserved)" or "Ready"
        if previous ~= nil then
            local deleted = previous:Delete()
            if deleted == false then error("unable to remove previous OutputPackagerConfig") end
        end
        new_config:SetAttrs({ TOOLS_Name = "OutputPackagerConfig" })
        new_config:SetData("OutputPackager.Role", apply.CONFIG_ROLE)
        new_config:SetData("OutputPackager.SchemaVersion", apply.SCHEMA_VERSION)
        set_flow_position(comp, new_config, x, y)
        comp:SetActiveTool(new_config)
    end)
    if not ok then
        if new_config ~= nil then pcall(function() new_config:Delete() end) end
        pcall(function() comp:Unlock() end)
        comp:EndUndo(false)
        error("OutputPackagerConfig build failed: " .. tostring(failure))
    end
    comp:Unlock()
    comp:EndUndo(true)
    print(previous and
        "[OutputPackager] Rebuilt OutputPackagerConfig; targets preserved" or
        "[OutputPackager] Created OutputPackagerConfig v0.1")
    return new_config
end

if rawget(_G, "comp") ~= nil then M.run(comp) end

return M
