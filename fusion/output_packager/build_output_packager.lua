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
    while comp:FindTool("OutputPackager" .. tostring(index)) ~= nil do
        index = index + 1
    end
    return "OutputPackager" .. tostring(index)
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
                        SourceOp = "OP_InputRouter",
                        Source = "Input",
                        Name = "Input",
                    },
                    OP_EnableReviewRaster = InstanceInput {
                        SourceOp = "OP_ReviewRasterSwitch",
                        Source = "Source",
                        Name = "Review Raster",
                        Page = "Output",
                        Default = %d,
                    },
                    OP_EnableWIP = InstanceInput {
                        SourceOp = "OP_WIPSwitch",
                        Source = "Source",
                        Name = "WIP Review",
                        Page = "Output",
                        Default = %d,
                    },
                    OP_ReviewWidth = InstanceInput {
                        SourceOp = "OP_ReviewCanvas",
                        Source = "Width",
                        Name = "Review Width",
                        Page = "Applied",
                        Default = %d,
                    },
                    OP_ReviewHeight = InstanceInput {
                        SourceOp = "OP_ReviewCanvas",
                        Source = "Height",
                        Name = "Review Height",
                        Page = "Applied",
                        Default = %d,
                    },
                    OP_CropRatio = InstanceInput {
                        SourceOp = "OP_WIPReview",
                        Source = "blankingAspectCustom",
                        Name = "Crop Ratio",
                        Page = "Applied",
                        Default = %.12g,
                    },
                    OP_Status = Input { Value = %q, },
                },
                Outputs = {
                    MainOutput1 = InstanceOutput {
                        SourceOp = "OP_WIPSwitch",
                        Source = "Output",
                        Name = "Output",
                    },
                },
                ViewInfo = GroupInfo { Pos = { 0, 0 } },
                Tools = ordered() {
                    OP_InputRouter = PipeRouter {
                        CtrlWShown = false,
                        NameSet = true,
                        ViewInfo = OperatorInfo { Pos = { -440, 0 } },
                    },
                    OP_ReviewResize = BetterResize {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Width = Input { Expression = "OP_ReviewCanvas.Width", },
                            Height = Input { Expression = "OP_ReviewCanvas.Height", },
                            KeepAspect = Input { Value = 1, },
                            FilterMethod = Input { Value = 3, },
                            Input = Input {
                                SourceOp = "OP_InputRouter",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -330, -1 } },
                    },
                    OP_ReviewCanvas = Background {
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
                        ViewInfo = OperatorInfo { Pos = { -330, 1 } },
                    },
                    OP_ReviewMerge = Merge {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            PerformDepthMerge = Input { Value = 0, },
                            Background = Input {
                                SourceOp = "OP_ReviewCanvas",
                                Source = "Output",
                            },
                            Foreground = Input {
                                SourceOp = "OP_ReviewResize",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -220, -1 } },
                    },
                    OP_ReviewRasterSwitch = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Source Raster", },
                            Name1 = Input { Value = "Review Raster", },
                            Input0 = Input {
                                SourceOp = "OP_InputRouter",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "OP_ReviewMerge",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { -110, 0 } },
                    },
                    OP_WIPReview = ofx.com.jtorrens.WIPReviewProbe {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            canvasMode = Input { Value = 0, },
                            placementMode = Input { Value = 0, },
                            resampleFilter = Input { Value = 2, },
                            blankingEnabled = Input { Value = 0, },
                            blankingAspectPreset = Input { Value = 4, },
                            blankingAspectCustom = Input { Value = %.12g, },
                            AllowResize = Input { Value = 0, },
                            Source = Input {
                                SourceOp = "OP_ReviewRasterSwitch",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 0, -1 } },
                    },
                    OP_WIPSwitch = Switch {
                        CtrlWShown = false,
                        NameSet = true,
                        Inputs = {
                            Source = Input { Value = %d, },
                            Name0 = Input { Value = "Clean", },
                            Name1 = Input { Value = "WIP Review", },
                            Input0 = Input {
                                SourceOp = "OP_ReviewRasterSwitch",
                                Source = "Output",
                            },
                            Input1 = Input {
                                SourceOp = "OP_WIPReview",
                                Source = "Output",
                            },
                        },
                        ViewInfo = OperatorInfo { Pos = { 110, 0 } },
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

if rawget(_G, "comp") ~= nil then
    M.last_group = M.run(comp, rawget(_G, "OUTPUTPACKAGER_OVERRIDES"))
end

return M
