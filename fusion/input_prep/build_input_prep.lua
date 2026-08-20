-- InputPrep v0.1 host prototype builder for Fusion Standalone 21.
-- Creates one connected GroupOperator in the active composition.

local M = {}

M.ROLE = "InputPrep"
M.SCHEMA_VERSION = 1
M.DEFAULTS = {
    workingWidth = 3840,
    workingHeight = 2160,
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

local function even(value)
    local rounded = math.floor(tonumber(value) + 0.5)
    if rounded % 2 ~= 0 then rounded = rounded - 1 end
    return math.max(2, rounded)
end

function M.crop_dimensions(width, height, ratio)
    width = even(width)
    height = even(height)
    ratio = tonumber(ratio)
    if ratio == nil or ratio <= 0 then error("Crop Ratio must be positive") end
    if width / height > ratio then
        width = even(height * ratio)
    else
        height = even(width / ratio)
    end
    return width, height
end

local function normalized_values(overrides)
    local values = {}
    for key, value in pairs(M.DEFAULTS) do values[key] = value end
    for key, value in pairs(overrides or {}) do values[key] = value end
    values.workingWidth = even(values.workingWidth)
    values.workingHeight = even(values.workingHeight)
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
    while comp:FindTool("InputPrep" .. tostring(index)) ~= nil do
        index = index + 1
    end
    return "InputPrep" .. tostring(index)
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
                        SourceOp = "IP_InputRouter",
                        Source = "Input",
                        Name = "Input",
                    },
                    IP_EnableDepth = InstanceInput {
                        SourceOp = "IP_DepthSwitch",
                        Source = "Source",
                        Name = "Change Depth",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_Depth = InstanceInput {
                        SourceOp = "IP_ChangeDepth",
                        Source = "Depth",
                        Name = "Depth",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_EnableColor = InstanceInput {
                        SourceOp = "IP_ColorSwitch",
                        Source = "Source",
                        Name = "Color Transform",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_EnableResize = InstanceInput {
                        SourceOp = "IP_ResizeSwitch",
                        Source = "Source",
                        Name = "Resize",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_EnableCrop = InstanceInput {
                        SourceOp = "IP_CropSwitch",
                        Source = "Source",
                        Name = "Crop",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_EmbeddedAlpha = InstanceInput {
                        SourceOp = "IP_AlphaSwitch",
                        Source = "Source",
                        Name = "Use Embedded Alpha",
                        Page = "InputPrep",
                        Default = %d,
                    },
                    IP_Status = Input { Value = %q, },
                },
                Outputs = {
                    MainOutput1 = InstanceOutput {
                        SourceOp = "IP_AlphaSwitch",
                        Source = "Output",
                        Name = "Output",
                    },
                },
                ViewInfo = GroupInfo { Pos = { 0, 0 } },
                Tools = ordered() {
                    IP_InputRouter = PipeRouter {
                        CtrlWShown = false,
                        NameSet = true,
                        ViewInfo = OperatorInfo { Pos = { -770, 0 } },
                    },
                    IP_ChangeDepth = ChangeDepth {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Depth = Input { Value = %d, },
                            Input = Input {
                                SourceOp = "IP_InputRouter",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -660, -1 } },
                    },
                    IP_DepthSwitch = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Bypass", },
                            Name1 = Input { Value = "Process", },
                            Input0 = Input {
                                SourceOp = "IP_InputRouter",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "IP_ChangeDepth",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -550, 0 } },
                    },
                    IP_Unpremultiply = AlphaDivide {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Input = Input {
                                SourceOp = "IP_DepthSwitch",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -440, -1 } },
                    },
                    IP_ColorTransform = ColorSpaceTransform {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            InputColorSpace = Input { Value = FuID { %q }, },
                            InputGamma = Input { Value = FuID { %q }, },
                            OutputColorSpace = Input { Value = FuID { %q }, },
                            OutputGamma = Input { Value = FuID { %q }, },
                            Input = Input {
                                SourceOp = "IP_Unpremultiply",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -330, -1 } },
                    },
                    IP_Premultiply = AlphaMultiply {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Input = Input {
                                SourceOp = "IP_ColorTransform",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -220, -1 } },
                    },
                    IP_ColorSwitch = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Bypass", },
                            Name1 = Input { Value = "Process", },
                            Input0 = Input {
                                SourceOp = "IP_DepthSwitch",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "IP_Premultiply",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -110, 0 } },
                    },
                    IP_Resize = BetterResize {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Width = Input { Value = %d, },
                            Height = Input { Value = %d, },
                            KeepAspect = Input { Value = 0, },
                            FilterMethod = Input { Value = 2, },
                            Input = Input {
                                SourceOp = "IP_ColorSwitch",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 0, -1 } },
                    },
                    IP_ResizeSwitch = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Bypass", },
                            Name1 = Input { Value = "Process", },
                            Input0 = Input {
                                SourceOp = "IP_ColorSwitch",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "IP_Resize",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 110, 0 } },
                    },
                    IP_Crop = Crop {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            XSize = Input { Value = %d, },
                            YSize = Input { Value = %d, },
                            XOffset = Input { Value = 0, },
                            YOffset = Input { Value = 0, },
                            KeepCentered = Input { Value = 1, },
                            Input = Input {
                                SourceOp = "IP_ResizeSwitch",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 220, -1 } },
                    },
                    IP_CropSwitch = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Bypass", },
                            Name1 = Input { Value = "Process", },
                            Input0 = Input {
                                SourceOp = "IP_ResizeSwitch",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "IP_Crop",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 330, 0 } },
                    },
                    IP_AlphaPolicy = ChannelBoolean {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            ToRed = Input { Value = 4, },
                            ToGreen = Input { Value = 4, },
                            ToBlue = Input { Value = 4, },
                            ToAlpha = Input { Value = 16, },
                            Background = Input {
                                SourceOp = "IP_CropSwitch",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 440, -1 } },
                    },
                    IP_AlphaSwitch = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Opaque", },
                            Name1 = Input { Value = "Preserve", },
                            Input0 = Input {
                                SourceOp = "IP_AlphaPolicy",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "IP_CropSwitch",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 550, 0 } },
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
        values.colorSelection, values.resizeSelection,
        values.cropSelection, values.alphaSelection, status,
        values.depth, values.depthSelection,
        values.sourceColorSpace, values.sourceGamma,
        values.workingColorSpace, values.workingGamma,
        values.colorSelection,
        values.workingWidth, values.workingHeight,
        values.resizeSelection,
        values.cropWidth, values.cropHeight,
        values.cropSelection,
        values.alphaSelection, name)
end

function M.run(comp_override, overrides)
    local active_comp = comp_override or rawget(_G, "comp")
    if active_comp == nil then error("InputPrep builder requires an active composition") end
    local values = normalized_values(overrides)
    local name = next_name(active_comp)
    local parsed = bmd.readstring(definition(name, values))
    if parsed == nil then error("Fusion could not parse the InputPrep definition") end

    active_comp:StartUndo("Build InputPrep v0.1 prototype")
    active_comp:Lock()
    local group = nil
    local ok, failure = pcall(function()
        local pasted = active_comp:Paste(parsed)
        if pasted == false then error("Fusion could not paste InputPrep") end
        group = active_comp:FindTool(name)
        if group == nil then error("pasted InputPrep was not found") end
        group:SetData("InputPrep.Role", M.ROLE)
        group:SetData("InputPrep.SchemaVersion", M.SCHEMA_VERSION)
        group:SetData("InputPrep.Prototype", true)
        active_comp:SetActiveTool(group)
    end)
    active_comp:Unlock()
    active_comp:EndUndo(ok)
    if not ok then
        if group ~= nil then pcall(function() group:Delete() end) end
        error("InputPrep build failed: " .. tostring(failure))
    end
    log(string.format("Created %s prototype: %dx%d -> %dx%d",
        name, values.workingWidth, values.workingHeight,
        values.cropWidth, values.cropHeight))
    return group
end

if rawget(_G, "comp") ~= nil then
    M.last_group = M.run(comp)
end

return M
