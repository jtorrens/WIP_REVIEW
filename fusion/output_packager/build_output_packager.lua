-- OutputPackager v0.1 image-component builder for Fusion Standalone 21.

local M = {}

M.ROLE = "OutputPackager"
M.SCHEMA_VERSION = 1
M.DEFAULTS = {
    reviewWidth = 1920,
    reviewHeight = 1080,
    cropRatio = 2.0,
    enableReviewRaster = true,
    enableWIP = false,
}

local function positive_integer(value, label)
    local number = tonumber(value)
    if number == nil or number <= 0 then error(label .. " must be positive") end
    return math.floor(number + 0.5)
end

local function normalized_values(overrides)
    local values = {}
    for key, value in pairs(M.DEFAULTS) do values[key] = value end
    for key, value in pairs(overrides or {}) do values[key] = value end
    values.reviewWidth = positive_integer(values.reviewWidth, "reviewWidth")
    values.reviewHeight = positive_integer(values.reviewHeight, "reviewHeight")
    values.cropRatio = tonumber(values.cropRatio)
    if values.cropRatio == nil or values.cropRatio <= 0 then
        error("cropRatio must be positive")
    end
    values.reviewSelection = values.enableReviewRaster and 1 or 0
    values.wipSelection = values.enableWIP and 1 or 0
    if values.wipSelection == 1 and values.reviewSelection == 0 then
        error("WIP Review requires Review Raster")
    end
    return values
end

local function next_name(comp)
    local index = 1
    while comp:FindTool("Group_OutputPackager" .. tostring(index)) ~= nil do
        index = index + 1
    end
    return "Group_OutputPackager" .. tostring(index)
end

local function definition(name, values)
    local status = string.format(
        "Review: %d x %d  |  Crop: %.4g  |  WIP: %s",
        values.reviewWidth, values.reviewHeight, values.cropRatio,
        values.wipSelection == 1 and "on" or "off")
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
                    OP_OutputColorSpace = InstanceInput {
                        SourceOp = "Custom_OutputMetadata",
                        Source = "OutputColorSpace",
                        Name = "Output Color Space",
                        Page = "Output Metadata",
                    },
                    OP_OutputGamma = InstanceInput {
                        SourceOp = "Custom_OutputMetadata",
                        Source = "OutputGamma",
                        Name = "Output Gamma",
                        Page = "Output Metadata",
                    },
                    OP_OutputFormat = InstanceInput {
                        SourceOp = "Custom_OutputMetadata",
                        Source = "OutputFormat",
                        Name = "Output Format",
                        Page = "Output Metadata",
                    },
                    OP_Compression = InstanceInput {
                        SourceOp = "Custom_OutputMetadata",
                        Source = "Compression",
                        Name = "Compression",
                        Page = "Output Metadata",
                    },
                    OP_EnableReviewRaster = InstanceInput {
                        SourceOp = "Switch_ReviewRaster",
                        Source = "Source",
                        Name = "Review Raster",
                        Page = "Output",
                        Default = %d,
                    },
                    OP_EnableWIP = InstanceInput {
                        SourceOp = "Switch_WIP",
                        Source = "Source",
                        Name = "WIP Review",
                        Page = "Output",
                        Default = %d,
                    },
                    OP_BlankingEnabled = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "blankingEnabled",
                        Name = "Blanking",
                        Page = "WIP",
                    },
                    OP_BlankingOpacity = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "blankingOpacity",
                        Name = "Blanking Opacity",
                        Page = "WIP",
                    },
                    OP_FontFamily = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "fontFamily",
                        Name = "Font Family",
                        Page = "WIP",
                    },
                    OP_FontSize = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "fontSize",
                        Name = "Font Size",
                        Page = "WIP",
                    },
                    OP_TextOpacity = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "textOpacity",
                        Name = "Text Opacity",
                        Page = "WIP",
                    },
                    OP_OutlineEnabled = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "outlineEnabled",
                        Name = "Outline",
                        Page = "WIP",
                    },
                    OP_OutlineWidth = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "outlineWidth",
                        Name = "Outline Width",
                        Page = "WIP",
                    },
                    OP_ShadowEnabled = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "shadowEnabled",
                        Name = "Shadow",
                        Page = "WIP",
                    },
                    OP_ShadowOffsetX = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "shadowOffsetX",
                        Name = "Shadow Offset X",
                        Page = "WIP",
                    },
                    OP_ShadowOffsetY = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "shadowOffsetY",
                        Name = "Shadow Offset Y",
                        Page = "WIP",
                    },
                    OP_ShadowSoftness = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "shadowSoftness",
                        Name = "Shadow Softness",
                        Page = "WIP",
                    },
                    OP_ShadowOpacity = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "shadowOpacity",
                        Name = "Shadow Opacity",
                        Page = "WIP",
                    },
                    OP_TLEnabled = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "tlEnabled",
                        Name = "Top Left",
                        Page = "WIP",
                    },
                    OP_TLText = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "tlPrefix",
                        Name = "Top Left Text",
                        Page = "WIP",
                    },
                    OP_TLCalculatedField = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "tlCalculatedField",
                        Name = "Top Left Calculated Field",
                        Page = "WIP",
                    },
                    OP_TCEnabled = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "tcEnabled",
                        Name = "Top Center",
                        Page = "WIP",
                    },
                    OP_TCText = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "tcPrefix",
                        Name = "Top Center Text",
                        Page = "WIP",
                    },
                    OP_TCCalculatedField = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "tcCalculatedField",
                        Name = "Top Center Calculated Field",
                        Page = "WIP",
                    },
                    OP_TREnabled = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "trEnabled",
                        Name = "Top Right",
                        Page = "WIP",
                    },
                    OP_TRText = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "trPrefix",
                        Name = "Top Right Text",
                        Page = "WIP",
                    },
                    OP_TRCalculatedField = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "trCalculatedField",
                        Name = "Top Right Calculated Field",
                        Page = "WIP",
                    },
                    OP_BLEnabled = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "blEnabled",
                        Name = "Bottom Left",
                        Page = "WIP",
                    },
                    OP_BLText = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "blPrefix",
                        Name = "Bottom Left Text",
                        Page = "WIP",
                    },
                    OP_BLCalculatedField = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "blCalculatedField",
                        Name = "Bottom Left Calculated Field",
                        Page = "WIP",
                    },
                    OP_BCEnabled = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "bcEnabled",
                        Name = "Bottom Center",
                        Page = "WIP",
                    },
                    OP_BCText = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "bcPrefix",
                        Name = "Bottom Center Text",
                        Page = "WIP",
                    },
                    OP_BCCalculatedField = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "bcCalculatedField",
                        Name = "Bottom Center Calculated Field",
                        Page = "WIP",
                    },
                    OP_BREnabled = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "brEnabled",
                        Name = "Bottom Right",
                        Page = "WIP",
                    },
                    OP_BRText = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "brPrefix",
                        Name = "Bottom Right Text",
                        Page = "WIP",
                    },
                    OP_BRCalculatedField = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "brCalculatedField",
                        Name = "Bottom Right Calculated Field",
                        Page = "WIP",
                    },
                    OP_FrameRelativeBase = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "frameRelativeBase",
                        Name = "Frame Relative Base",
                        Page = "WIP",
                    },
                    OP_FrameStart = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "frameStart",
                        Name = "Frame Start",
                        Page = "WIP",
                    },
                    OP_FPSMode = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "fpsMode",
                        Name = "FPS Mode",
                        Page = "WIP",
                    },
                    OP_FPSOverride = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "fpsOverride",
                        Name = "FPS Override",
                        Page = "WIP",
                    },
                    OP_TimecodeStart = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "timecodeStart",
                        Name = "Timecode Start",
                        Page = "WIP",
                    },
                    OP_DropFrameMode = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "dropFrameMode",
                        Name = "Drop Frame Mode",
                        Page = "WIP",
                    },
                    OP_ColorSpaceMode = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "colorSpaceMode",
                        Name = "Color Space Mode",
                        Page = "WIP",
                    },
                    OP_ManualColorSpace = InstanceInput {
                        SourceOp = "WIPReviewProbe_WIP",
                        Source = "manualColorSpace",
                        Name = "Manual Color Space",
                        Page = "WIP",
                    },
                    OP_ReviewWidth = InstanceInput {
                        SourceOp = "Background_ReviewCanvas",
                        Source = "Width",
                        Name = "Review Width",
                        Page = "Applied",
                        Default = %d,
                    },
                    OP_ReviewHeight = InstanceInput {
                        SourceOp = "Background_ReviewCanvas",
                        Source = "Height",
                        Name = "Review Height",
                        Page = "Applied",
                        Default = %d,
                    },
                    OP_CropRatio = InstanceInput {
                        SourceOp = "Custom_AppliedData",
                        Source = "NumberIn1",
                        Name = "Crop Ratio",
                        Page = "Applied",
                        Default = %.12g,
                    },
                    OP_Status = Input { Value = %q, },
                },
                Outputs = {
                    MainOutput1 = InstanceOutput {
                        SourceOp = "PipeRouter_GroupOutput",
                        Source = "Output",
                        Name = "Output",
                    },
                },
                ViewInfo = GroupInfo { Pos = { 0, 0 } },
                Tools = ordered() {
                    PipeRouter_Input = PipeRouter {
                        CtrlWShown = false,
                        NameSet = true,
                        ViewInfo = PipeRouterInfo { Pos = { -275, 72 } },
                    },
                    BetterResize_ReviewRaster = BetterResize {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Width = Input { Expression = "Background_ReviewCanvas.Width", },
                            Height = Input { Expression = "Background_ReviewCanvas.Height", },
                            KeepAspect = Input { Value = 1, },
                            FilterMethod = Input { Value = 3, },
                            Input = Input {
                                SourceOp = "PipeRouter_Input",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -110, 72 } },
                    },
                    Background_ReviewCanvas = Background {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Width = Input { Value = %d, },
                            Height = Input { Value = %d, },
                            TopLeftRed = Input { Value = 0, },
                            TopLeftGreen = Input { Value = 0, },
                            TopLeftBlue = Input { Value = 0, },
                            TopLeftAlpha = Input { Value = 1, },
                        },
                        ViewInfo = OperatorInfo { Pos = { 55, 144 } },
                    },
                    Merge_ReviewRaster = Merge {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            PerformDepthMerge = Input { Value = 0, },
                            Background = Input {
                                SourceOp = "Background_ReviewCanvas",
                                Source = "Output",
                            },
                            Foreground = Input {
                                SourceOp = "BetterResize_ReviewRaster",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -110, 144 } },
                    },
                    Switch_ReviewRaster = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Source Raster", },
                            Name1 = Input { Value = "Review Raster", },
                            Input0 = Input {
                                SourceOp = "PipeRouter_SourceBypass",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "Merge_ReviewRaster",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -110, 216 } },
                    },
                    Custom_AppliedData = Custom {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            NumberIn1 = Input { Value = %.12g, },
                        },
                        ViewInfo = OperatorInfo { Pos = { -165, 0 } },
                    },
                    Custom_OutputMetadata = Custom {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            OutputColorSpace = Input { Value = "REC709_COLORSPACE", },
                            OutputGamma = Input { Value = "LINEAR_GAMMA", },
                            OutputFormat = Input { Value = "Movie", },
                            Compression = Input { Value = "ProRes 422 HQ", },
                        },
                        UserControls = ordered() {
                            OutputColorSpace = {
                                LINKS_Name = "Output Color Space",
                                LINKID_DataType = "Text",
                                INPID_InputControl = "TextEditControl",
                                TEC_ReadOnly = true,
                                ICS_ControlPage = "Output Metadata",
                            },
                            OutputGamma = {
                                LINKS_Name = "Output Gamma",
                                LINKID_DataType = "Text",
                                INPID_InputControl = "TextEditControl",
                                TEC_ReadOnly = true,
                                ICS_ControlPage = "Output Metadata",
                            },
                            OutputFormat = {
                                LINKS_Name = "Output Format",
                                LINKID_DataType = "Text",
                                INPID_InputControl = "TextEditControl",
                                TEC_ReadOnly = true,
                                ICS_ControlPage = "Output Metadata",
                            },
                            Compression = {
                                LINKS_Name = "Compression",
                                LINKID_DataType = "Text",
                                INPID_InputControl = "TextEditControl",
                                TEC_ReadOnly = true,
                                ICS_ControlPage = "Output Metadata",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -165, -72 } },
                    },
                    WIPReviewProbe_WIP = ofx.com.jtorrens.WIPReviewProbe {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            canvasMode = Input { Value = 0, },
                            placementMode = Input { Value = 0, },
                            resampleFilter = Input { Value = 2, },
                            blankingEnabled = Input { Value = 0, },
                            blankingAspectPreset = Input { Value = 4, },
                            blankingAspectCustom = Input {
                                Expression = "Custom_AppliedData.NumberIn1",
                            },
                            AllowResize = Input { Value = 0, },
                            Source = Input {
                                SourceOp = "PipeRouter_WIPSource",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 110, 288 } },
                    },
                    Switch_WIP = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Clean", },
                            Name1 = Input { Value = "WIP Review", },
                            Input0 = Input {
                                SourceOp = "Switch_ReviewRaster",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "WIPReviewProbe_WIP",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -55, 288 } },
                    },
                    Custom_LegalClamp = Custom {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            RedExpression = Input { Value = "max(r1, 0)", },
                            GreenExpression = Input { Value = "max(g1, 0)", },
                            BlueExpression = Input { Value = "max(b1, 0)", },
                            AlphaExpression = Input { Value = "min(max(a1, 0), 1)", },
                            Image1 = Input {
                                SourceOp = "Switch_WIP",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -55, 360 } },
                    },
                    PipeRouter_Output = PipeRouter {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Input = Input {
                                SourceOp = "Custom_LegalClamp",
                                Source = "Output",
                            },
                        },
                        ViewInfo = PipeRouterInfo { Pos = { -55, 432 } },
                    },
                    PipeRouter_WIPSource = PipeRouter {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Input = Input {
                                SourceOp = "Switch_ReviewRaster",
                                Source = "Output",
                            },
                        },
                        ViewInfo = PipeRouterInfo { Pos = { 110, 216 } },
                    },
                    PipeRouter_GroupOutput = PipeRouter {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Input = Input {
                                SourceOp = "PipeRouter_OutputReturn",
                                Source = "Output",
                            },
                        },
                        ViewInfo = PipeRouterInfo { Pos = { 220, 0 } },
                    },
                    PipeRouter_SourceBypass = PipeRouter {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Input = Input {
                                SourceOp = "PipeRouter_Input",
                                Source = "Output",
                            },
                        },
                        ViewInfo = PipeRouterInfo { Pos = { -275, 216 } },
                    },
                    PipeRouter_OutputReturn = PipeRouter {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Input = Input {
                                SourceOp = "PipeRouter_Output",
                                Source = "Output",
                            },
                        },
                        ViewInfo = PipeRouterInfo { Pos = { 220, 360 } },
                    },
                },
                UserControls = ordered() {
                    OP_Status = {
                        LINKS_Name = "Applied Configuration",
                        LINKID_DataType = "Text",
                        INPID_InputControl = "TextEditControl",
                        INP_ReadOnly = true,
                        INP_External = false,
                        INP_ForceNotify = false,
                        ICS_ControlPage = "Output",
                    },
                },
            },
        },
        ActiveTool = %q,
    }
    ]], name,
        values.reviewSelection, values.wipSelection,
        values.reviewWidth, values.reviewHeight, values.cropRatio, status,
        values.reviewWidth, values.reviewHeight, values.reviewSelection,
        values.cropRatio, values.wipSelection, name)
end

local function paste_group(comp, overrides, name)
    local values = normalized_values(overrides)
    local parsed = bmd.readstring(definition(name, values))
    if parsed == nil then error("Fusion could not parse OutputPackager") end
    local pasted = comp:Paste(parsed)
    if pasted == false then error("Fusion could not paste OutputPackager") end
    local group = comp:FindTool(name)
    if group == nil then error("pasted OutputPackager was not found") end
    group:SetData("OutputPackager.Role", M.ROLE)
    group:SetData("OutputPackager.SchemaVersion", M.SCHEMA_VERSION)
    return group, values
end

local PUBLIC_CONTROLS = {
    "OP_EnableReviewRaster",
    "OP_EnableWIP",
    "OP_BlankingEnabled", "OP_BlankingOpacity",
    "OP_FontFamily", "OP_FontSize", "OP_TextOpacity",
    "OP_OutlineEnabled", "OP_OutlineWidth",
    "OP_ShadowEnabled", "OP_ShadowOffsetX", "OP_ShadowOffsetY",
    "OP_ShadowSoftness", "OP_ShadowOpacity",
    "OP_TLEnabled", "OP_TLText", "OP_TLCalculatedField",
    "OP_TCEnabled", "OP_TCText", "OP_TCCalculatedField",
    "OP_TREnabled", "OP_TRText", "OP_TRCalculatedField",
    "OP_BLEnabled", "OP_BLText", "OP_BLCalculatedField",
    "OP_BCEnabled", "OP_BCText", "OP_BCCalculatedField",
    "OP_BREnabled", "OP_BRText", "OP_BRCalculatedField",
    "OP_FrameRelativeBase", "OP_FrameStart", "OP_FPSMode",
    "OP_FPSOverride", "OP_TimecodeStart", "OP_DropFrameMode",
    "OP_ColorSpaceMode", "OP_ManualColorSpace",
    "OP_ReviewWidth",
    "OP_ReviewHeight",
    "OP_CropRatio",
    "OP_Status",
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
    local comp = comp_override or rawget(_G, "comp")
    if comp == nil then error("OutputPackager builder requires an active composition") end
    local name = next_name(comp)
    comp:StartUndo("Build OutputPackager v0.1")
    comp:Lock()
    local group = nil
    local ok, failure = pcall(function()
        group = paste_group(comp, overrides, name)
        comp:SetActiveTool(group)
    end)
    comp:Unlock()
    comp:EndUndo(ok)
    if not ok then
        if group ~= nil then pcall(function() group:Delete() end) end
        error("OutputPackager build failed: " .. tostring(failure))
    end
    print("[OutputPackager] Created " .. name)
    return group
end

function M.rebuild(comp_override, target)
    local comp = comp_override or rawget(_G, "comp")
    if comp == nil then error("OutputPackager rebuild requires an active composition") end
    if target == nil or target:GetData("OutputPackager.Role") ~= M.ROLE then
        error("select exactly one OutputPackager to rebuild")
    end
    if tonumber(target:GetData("OutputPackager.SchemaVersion")) ~= M.SCHEMA_VERSION then
        error("selected OutputPackager has an unsupported schema")
    end

    local time = comp.CurrentTime or 0
    local original_name = target:GetAttrs().TOOLS_Name
    local saved = {}
    for _, id in ipairs(PUBLIC_CONTROLS) do
        if target[id] == nil then error("selected OutputPackager is missing " .. id) end
        saved[id] = target[id][time]
    end
    local upstream = target.MainInput1 and target.MainInput1:GetConnectedOutput() or nil
    local old_output = first_output(target)
    if old_output == nil then error("selected OutputPackager has no output") end
    local consumers = old_output:GetConnectedInputs() or {}
    local x, y = flow_position(comp, target)
    local temporary_name = unique_name(comp, "OutputPackagerRebuild")
    local backup_name = unique_name(comp, "OutputPackagerPrevious")

    comp:StartUndo("Rebuild OutputPackager v0.1")
    comp:Lock()
    local replacement = nil
    local new_output = nil
    local old_renamed = false
    local new_renamed = false
    local old_deleted = false
    local ok, failure = pcall(function()
        replacement = paste_group(comp, nil, temporary_name)
        for _, id in ipairs(PUBLIC_CONTROLS) do
            replacement[id][time] = saved[id]
        end
        if upstream ~= nil then
            local connected = replacement.MainInput1:ConnectTo(upstream)
            if connected == false then error("unable to restore Input connection") end
        end
        new_output = first_output(replacement)
        if new_output == nil then error("replacement OutputPackager has no output") end
        for _, destination in pairs(consumers) do
            local connected = destination:ConnectTo(new_output)
            if connected == false then error("unable to restore an Output connection") end
        end
        target:SetAttrs({ TOOLS_Name = backup_name })
        old_renamed = true
        replacement:SetAttrs({ TOOLS_Name = original_name })
        new_renamed = true
        set_flow_position(comp, replacement, x, y)
        comp:SetActiveTool(replacement)
        local deleted = target:Delete()
        if deleted == false then error("unable to remove previous OutputPackager") end
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
        set_flow_position(comp, target, x, y)
        pcall(function() comp:SetActiveTool(target) end)
    end
    comp:Unlock()
    comp:EndUndo(ok)
    if not ok then error("OutputPackager rebuild failed: " .. tostring(failure)) end
    print("[OutputPackager] Rebuilt " .. original_name ..
        "; values and connections preserved")
    return replacement
end

if rawget(_G, "comp") ~= nil then
    local selected = rawget(_G, "tool")
    if selected == nil then pcall(function() selected = comp.ActiveTool end) end
    if selected ~= nil and selected:GetData("OutputPackager.Role") == M.ROLE then
        M.last_group = M.rebuild(comp, selected)
    else
        M.last_group = M.run(comp, rawget(_G, "OUTPUTPACKAGER_OVERRIDES"))
    end
end

return M
