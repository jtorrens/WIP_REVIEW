-- InputPrep v0.1 builder for Fusion Standalone 21.
-- Creates one connected GroupOperator in the active composition.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local geometry = dofile(script_directory() .. "geometry.lua")
local M = {}

M.ROLE = "InputPrep"
M.SCHEMA_VERSION = 1
M.DEFAULTS = {
    workingWidth = 3840,
    workingHeight = 2160,
    sourceWidth = 3840,
    sourceHeight = 2160,
    cropRatio = 2.0,
    sourceColorSpace = "REC709_COLORSPACE",
    sourceGamma = "TWOPOINTFOUR_GAMMA",
    workingColorSpace = "REC709_COLORSPACE",
    workingGamma = "LINEAR_GAMMA",
    embeddedAlpha = false,
    depth = 3,
    enableDepth = true,
    enableColor = true,
    enableResize = true,
    enableCrop = true,
}

local function log(message)
    print("[InputPrep] " .. message)
end

M.crop_dimensions = geometry.crop_dimensions
M.fill_width = geometry.fill_width

local function normalized_values(overrides)
    local values = {}
    for key, value in pairs(M.DEFAULTS) do values[key] = value end
    for key, value in pairs(overrides or {}) do values[key] = value end
    values.workingWidth = geometry.round_even(values.workingWidth)
    values.workingHeight = geometry.round_even(values.workingHeight)
    values.resizeWidth = geometry.fill_width(
        values.sourceWidth, values.sourceHeight,
        values.workingWidth, values.workingHeight)
    values.cropWidth, values.cropHeight = M.crop_dimensions(
        values.workingWidth, values.workingHeight, values.cropRatio)
    values.depthSelection = values.enableDepth and 1 or 0
    values.colorSelection = values.enableColor and 1 or 0
    values.resizeSelection = values.enableResize and 1 or 0
    values.cropSelection = values.enableCrop and 1 or 0
    values.alphaSelection = values.embeddedAlpha and 1 or 0
    return values
end

local function next_name(comp)
    local index = 1
    while comp:FindTool("Group_InputPrep" .. tostring(index)) ~= nil do
        index = index + 1
    end
    return "Group_InputPrep" .. tostring(index)
end

local function definition(name, values)
    local status = string.format(
        "%d x %d  ->  %d x %d  |  Depth: %d  |  %s / %s  ->  %s / %s",
        values.workingWidth, values.workingHeight,
        values.cropWidth, values.cropHeight,
        values.depth,
        values.sourceColorSpace, values.sourceGamma,
        values.workingColorSpace, values.workingGamma)
    return string.format([[
    {
        Tools = ordered() {
            %s = GroupOperator {
                Inputs = ordered() {
                    MainInput1 = InstanceInput {
                        SourceOp = "PipeRouter_Input",
                        Source = "Input",
                        Name = "Input",
                    },
                    IP_EnableDepth = InstanceInput {
                        SourceOp = "Switch_Depth",
                        Source = "Source",
                        Name = "Change Depth",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_Depth = InstanceInput {
                        SourceOp = "ChangeDepth_Working",
                        Source = "Depth",
                        Name = "Depth",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_SourceColorSpace = InstanceInput {
                        SourceOp = "ColorSpaceTransform_Working",
                        Source = "InputColorSpace",
                        Name = "Source Color Space",
                        Page = "Applied",
                    },
                    IP_SourceGamma = InstanceInput {
                        SourceOp = "ColorSpaceTransform_Working",
                        Source = "InputGamma",
                        Name = "Source Gamma",
                        Page = "Applied",
                    },
                    IP_WorkingColorSpace = InstanceInput {
                        SourceOp = "ColorSpaceTransform_Working",
                        Source = "OutputColorSpace",
                        Name = "Working Color Space",
                        Page = "Applied",
                    },
                    IP_WorkingGamma = InstanceInput {
                        SourceOp = "ColorSpaceTransform_Working",
                        Source = "OutputGamma",
                        Name = "Working Gamma",
                        Page = "Applied",
                    },
                    IP_ResizeWidth = InstanceInput {
                        SourceOp = "BetterResize_Working",
                        Source = "Width",
                        Name = "Resize Width",
                        Page = "Applied",
                        Default = %d,
                    },
                    IP_ResizeHeight = InstanceInput {
                        SourceOp = "BetterResize_Working",
                        Source = "Height",
                        Name = "Working Height",
                        Page = "Applied",
                        Default = %d,
                    },
                    IP_CropWidth = InstanceInput {
                        SourceOp = "Crop_Working",
                        Source = "XSize",
                        Name = "Crop Width",
                        Page = "Applied",
                        Default = %d,
                    },
                    IP_CropHeight = InstanceInput {
                        SourceOp = "Crop_Working",
                        Source = "YSize",
                        Name = "Crop Height",
                        Page = "Applied",
                        Default = %d,
                    },
                    IP_EnableColor = InstanceInput {
                        SourceOp = "Switch_Color",
                        Source = "Source",
                        Name = "Color Transform",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_EnableResize = InstanceInput {
                        SourceOp = "Switch_Resize",
                        Source = "Source",
                        Name = "Resize",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_EnableCrop = InstanceInput {
                        SourceOp = "Switch_Crop",
                        Source = "Source",
                        Name = "Crop",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_EmbeddedAlpha = InstanceInput {
                        SourceOp = "Switch_Alpha",
                        Source = "Source",
                        Name = "Use Embedded Alpha",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_Status = Input { Value = %q, },
                },
                Outputs = {
                    MainOutput1 = InstanceOutput {
                        SourceOp = "Switch_Alpha",
                        Source = "Output",
                        Name = "Output",
                    },
                },
                ViewInfo = GroupInfo { Pos = { 0, 0 } },
                Tools = ordered() {
                    PipeRouter_Input = PipeRouter {
                        CtrlWShown = false,
                        NameSet = true,
                        ViewInfo = OperatorInfo { Pos = { -900, 0 } },
                    },
                    ChangeDepth_Working = ChangeDepth {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Depth = Input { Value = %d, },
                            Input = Input {
                                SourceOp = "PipeRouter_Input",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -800, -1.5 } },
                    },
                    Switch_Depth = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Bypass", },
                            Name1 = Input { Value = "Process", },
                            Input0 = Input {
                                SourceOp = "PipeRouter_Input",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "ChangeDepth_Working",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -700, 0 } },
                    },
                    AlphaDivide_Unpremultiply = AlphaDivide {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Input = Input {
                                SourceOp = "Switch_Depth",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -600, -1.5 } },
                    },
                    Switch_PreColorAlpha = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Expression = "Switch_Alpha.Source", },
                            Name0 = Input { Value = "Ignore Alpha", },
                            Name1 = Input { Value = "Unpremultiply", },
                            Input0 = Input {
                                SourceOp = "Switch_Depth",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "AlphaDivide_Unpremultiply",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -525, -1.5 } },
                    },
                    ColorSpaceTransform_Working = ColorSpaceTransform {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            InputColorSpace = Input { Value = FuID { %q }, },
                            InputGamma = Input { Value = FuID { %q }, },
                            OutputColorSpace = Input { Value = FuID { %q }, },
                            OutputGamma = Input { Value = FuID { %q }, },
                            Input = Input {
                                SourceOp = "Switch_PreColorAlpha",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -425, -1.5 } },
                    },
                    AlphaMultiply_Premultiply = AlphaMultiply {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Input = Input {
                                SourceOp = "ColorSpaceTransform_Working",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -325, -1.5 } },
                    },
                    Switch_PostColorAlpha = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Expression = "Switch_Alpha.Source", },
                            Name0 = Input { Value = "Straight", },
                            Name1 = Input { Value = "Premultiply", },
                            Input0 = Input {
                                SourceOp = "ColorSpaceTransform_Working",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "AlphaMultiply_Premultiply",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -250, -1.5 } },
                    },
                    Switch_Color = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Bypass", },
                            Name1 = Input { Value = "Process", },
                            Input0 = Input {
                                SourceOp = "Switch_Depth",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "Switch_PostColorAlpha",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -150, 0 } },
                    },
                    BetterResize_Working = BetterResize {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Width = Input { Value = %d, },
                            Height = Input { Value = %d, },
                            KeepAspect = Input { Value = 1, },
                            FilterMethod = Input { Value = 2, },
                            Input = Input {
                                SourceOp = "Switch_Color",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -25, -1.5 } },
                    },
                    Switch_Resize = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Bypass", },
                            Name1 = Input { Value = "Process", },
                            Input0 = Input {
                                SourceOp = "Switch_Color",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "BetterResize_Working",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 100, 0 } },
                    },
                    Crop_Working = Crop {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            XSize = Input { Value = %d, },
                            YSize = Input { Value = %d, },
                            XOffset = Input { Value = 0, },
                            YOffset = Input { Value = 0, },
                            KeepCentered = Input { Value = 1, },
                            Input = Input {
                                SourceOp = "Switch_Resize",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 225, -1.5 } },
                    },
                    Switch_Crop = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Bypass", },
                            Name1 = Input { Value = "Process", },
                            Input0 = Input {
                                SourceOp = "Switch_Resize",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "Crop_Working",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 350, 0 } },
                    },
                    ChannelBoolean_AlphaPolicy = ChannelBoolean {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            ToRed = Input { Value = 4, },
                            ToGreen = Input { Value = 4, },
                            ToBlue = Input { Value = 4, },
                            ToAlpha = Input { Value = 16, },
                            Background = Input {
                                SourceOp = "Switch_Crop",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 475, -1.5 } },
                    },
                    Switch_Alpha = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Opaque", },
                            Name1 = Input { Value = "Preserve", },
                            Input0 = Input {
                                SourceOp = "ChannelBoolean_AlphaPolicy",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "Switch_Crop",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 600, 0 } },
                    },
                },
                UserControls = ordered() {
                    IP_Status = {
                        LINKS_Name = "Applied Configuration",
                        LINKID_DataType = "Text",
                        INPID_InputControl = "TextEditControl",
                        INP_ReadOnly = true,
                        INP_External = false,
                        INP_ForceNotify = false,
                        ICS_ControlPage = "InputPrep",
                    },
                },
            },
        },
        ActiveTool = %q,
    }
    ]], name,
        values.depthSelection, values.depth,
        values.resizeWidth, values.workingHeight,
        values.cropWidth, values.cropHeight,
        values.colorSelection, values.resizeSelection,
        values.cropSelection, values.alphaSelection, status,
        values.depth, values.depthSelection,
        values.sourceColorSpace, values.sourceGamma,
        values.workingColorSpace, values.workingGamma,
        values.colorSelection,
        values.resizeWidth, values.workingHeight,
        values.resizeSelection,
        values.cropWidth, values.cropHeight,
        values.cropSelection,
        values.alphaSelection, name)
end

local PUBLIC_CONTROLS = {
    "IP_EnableDepth", "IP_Depth",
    "IP_SourceColorSpace", "IP_SourceGamma",
    "IP_WorkingColorSpace", "IP_WorkingGamma", "IP_EnableColor",
    "IP_ResizeWidth", "IP_ResizeHeight", "IP_EnableResize",
    "IP_CropWidth", "IP_CropHeight", "IP_EnableCrop",
    "IP_EmbeddedAlpha", "IP_Status",
}

local function first_output(tool)
    for _, output in pairs(tool:GetOutputList() or {}) do return output end
    return nil
end

local function unique_name(comp, prefix)
    local index = 1
    local name = prefix
    while comp:FindTool(name) ~= nil do
        index = index + 1
        name = prefix .. tostring(index)
    end
    return name
end

local function paste_group(comp, overrides, name)
    local values = normalized_values(overrides)
    local parsed = bmd.readstring(definition(name, values))
    if parsed == nil then error("Fusion could not parse the InputPrep definition") end
    local pasted = comp:Paste(parsed)
    if pasted == false then error("Fusion could not paste InputPrep") end
    local group = comp:FindTool(name)
    if group == nil then error("pasted InputPrep was not found") end
    group:SetData("InputPrep.Role", M.ROLE)
    group:SetData("InputPrep.SchemaVersion", M.SCHEMA_VERSION)
    return group, values
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

function M.run(comp_override, overrides)
    local active_comp = comp_override or rawget(_G, "comp")
    if active_comp == nil then error("InputPrep builder requires an active composition") end
    local name = next_name(active_comp)

    active_comp:StartUndo("Build InputPrep v0.1")
    active_comp:Lock()
    local group = nil
    local values = nil
    local ok, failure = pcall(function()
        group, values = paste_group(active_comp, overrides, name)
        active_comp:SetActiveTool(group)
    end)
    active_comp:Unlock()
    active_comp:EndUndo(ok)
    if not ok then
        if group ~= nil then pcall(function() group:Delete() end) end
        error("InputPrep build failed: " .. tostring(failure))
    end
    log(string.format("Created %s: %dx%d -> %dx%d",
        name, values.workingWidth, values.workingHeight,
        values.cropWidth, values.cropHeight))
    return group
end

function M.rebuild(comp_override, target)
    local active_comp = comp_override or rawget(_G, "comp")
    if active_comp == nil then error("InputPrep rebuild requires an active composition") end
    if target == nil or target:GetData("InputPrep.Role") ~= M.ROLE then
        error("select exactly one InputPrep processor to rebuild")
    end
    if tonumber(target:GetData("InputPrep.SchemaVersion")) ~= M.SCHEMA_VERSION then
        error("selected InputPrep has an unsupported schema")
    end

    local time = active_comp.CurrentTime or 0
    local original_name = target:GetAttrs().TOOLS_Name
    local saved = {}
    for _, id in ipairs(PUBLIC_CONTROLS) do
        if target[id] == nil then error("selected InputPrep is missing " .. id) end
        saved[id] = target[id][time]
    end
    local upstream = target.MainInput1 and target.MainInput1:GetConnectedOutput() or nil
    local old_output = first_output(target)
    if old_output == nil then error("selected InputPrep has no output") end
    local consumers = old_output:GetConnectedInputs() or {}
    local x, y = flow_position(active_comp, target)
    local temporary_name = unique_name(active_comp, "InputPrepRebuild")
    local backup_name = unique_name(active_comp, "InputPrepPrevious")

    active_comp:StartUndo("Rebuild InputPrep v0.1")
    active_comp:Lock()
    local replacement = nil
    local new_output = nil
    local old_renamed = false
    local new_renamed = false
    local old_deleted = false
    local ok, failure = pcall(function()
        replacement = paste_group(active_comp, nil, temporary_name)
        for _, id in ipairs(PUBLIC_CONTROLS) do
            replacement[id][time] = saved[id]
        end
        if upstream ~= nil then
            local connected = replacement.MainInput1:ConnectTo(upstream)
            if connected == false then error("unable to restore Input connection") end
        end
        new_output = first_output(replacement)
        if new_output == nil then error("replacement InputPrep has no output") end
        for _, destination in pairs(consumers) do
            local connected = destination:ConnectTo(new_output)
            if connected == false then error("unable to restore an Output connection") end
        end
        target:SetAttrs({ TOOLS_Name = backup_name })
        old_renamed = true
        replacement:SetAttrs({ TOOLS_Name = original_name })
        new_renamed = true
        set_flow_position(active_comp, replacement, x, y)
        active_comp:SetActiveTool(replacement)
        local deleted = target:Delete()
        if deleted == false then error("unable to remove previous InputPrep") end
        old_deleted = true
    end)

    if not ok and not old_deleted then
        for _, destination in pairs(consumers) do
            pcall(function() destination:ConnectTo(old_output) end)
        end
        if new_renamed and replacement ~= nil then
            pcall(function() replacement:SetAttrs({ TOOLS_Name = temporary_name }) end)
        end
        if old_renamed then
            pcall(function() target:SetAttrs({ TOOLS_Name = original_name }) end)
        end
        if replacement ~= nil then pcall(function() replacement:Delete() end) end
        set_flow_position(active_comp, target, x, y)
        pcall(function() active_comp:SetActiveTool(target) end)
    end
    active_comp:Unlock()
    active_comp:EndUndo(ok)
    if not ok then error("InputPrep rebuild failed: " .. tostring(failure)) end
    log("Rebuilt " .. original_name .. "; values and connections preserved")
    return replacement
end

if rawget(_G, "comp") ~= nil then
    local selected = rawget(_G, "tool")
    if selected == nil then
        pcall(function() selected = comp.ActiveTool end)
    end
    if selected ~= nil and selected:GetData("InputPrep.Role") == M.ROLE then
        M.last_group = M.rebuild(comp, selected)
    else
        M.last_group = M.run(comp, rawget(_G, "INPUTPREP_OVERRIDES"))
    end
end

return M
