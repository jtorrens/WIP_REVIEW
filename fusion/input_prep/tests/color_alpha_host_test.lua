-- Pixel comparison for InputPrep color and embedded-alpha policies.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local TEST_DIR = script_directory()
local INPUT_PREP_DIR = TEST_DIR .. "../"
local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local temp_comp = dofile(TEST_DIR .. "../../test_support/temp_comp.lua")
local comp = temp_comp.acquire(fusion)
if comp == nil then error("Fusion could not create the color/alpha test comp") end
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local function fail(message)
    error("InputPrep color/alpha failure: " .. message, 2)
end

local function add_tool(reg_id, name, x, y)
    local tool = comp:AddTool(reg_id, x, y)
    if tool == nil then fail("unable to add " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    return tool
end

local function set(tool, id, value)
    if tool[id] == nil then fail(id .. " is missing") end
    tool[id][comp.CurrentTime] = value
end

local function configure_processor(tool, embedded_alpha)
    set(tool, "IP_EnableDepth", 0)
    set(tool, "IP_EnableColor", 1)
    set(tool, "IP_EnableResize", 0)
    set(tool, "IP_EnableCrop", 0)
    set(tool, "IP_EmbeddedAlpha", embedded_alpha and 1 or 0)
    set(tool, "IP_SourceColorSpace", "REC709_COLORSPACE")
    set(tool, "IP_SourceGamma", "TWOPOINTFOUR_GAMMA")
    set(tool, "IP_WorkingColorSpace", "REC709_COLORSPACE")
    set(tool, "IP_WorkingGamma", "LINEAR_GAMMA")
end

local function first_output(tool)
    for _, output in pairs(tool:GetOutputList() or {}) do return output end
    fail(tool:GetAttrs().TOOLS_Name .. " has no output")
end

local function add_saver(name, path, output, y)
    local saver = add_tool("Saver", name, 2, y)
    saver.Clip[0] = path
    saver.Input = output
    return saver
end

comp:Lock()
local source = add_tool("Background", "AlphaPixelSource", -5, 0)
source.Width[0] = 16
source.Height[0] = 16
source.TopLeftRed[0] = 0.2
source.TopLeftGreen[0] = 0.1
source.TopLeftBlue[0] = 0.05
source.TopLeftAlpha[0] = 0.5
comp:Unlock()

_G.INPUTPREP_OVERRIDES = { sourceWidth = 16, sourceHeight = 16 }
local builder = dofile(INPUT_PREP_DIR .. "build_input_prep.lua")
_G.INPUTPREP_OVERRIDES = nil
local opaque_processor = builder.last_group
local embedded_processor = builder.run(comp, { sourceWidth = 16, sourceHeight = 16 })
opaque_processor.MainInput1 = source.Output
embedded_processor.MainInput1 = source.Output
configure_processor(opaque_processor, false)
configure_processor(embedded_processor, true)

comp:Lock()
local straight_cst = add_tool("ColorSpaceTransform", "ReferenceStraightCST", -3, 2)
straight_cst.InputColorSpace[0] = "REC709_COLORSPACE"
straight_cst.InputGamma[0] = "TWOPOINTFOUR_GAMMA"
straight_cst.OutputColorSpace[0] = "REC709_COLORSPACE"
straight_cst.OutputGamma[0] = "LINEAR_GAMMA"
straight_cst.Input = source.Output
local opaque = add_tool("ChannelBoolean", "ReferenceOpaque", -1, 2)
opaque.ToRed[0] = 4
opaque.ToGreen[0] = 4
opaque.ToBlue[0] = 4
opaque.ToAlpha[0] = 16
opaque.Background = straight_cst.Output

local divide = add_tool("AlphaDivide", "ReferenceDivide", -4, 3)
divide.Input = source.Output
local premult_cst = add_tool("ColorSpaceTransform", "ReferencePremultCST", -3, 3)
premult_cst.InputColorSpace[0] = "REC709_COLORSPACE"
premult_cst.InputGamma[0] = "TWOPOINTFOUR_GAMMA"
premult_cst.OutputColorSpace[0] = "REC709_COLORSPACE"
premult_cst.OutputGamma[0] = "LINEAR_GAMMA"
premult_cst.Input = divide.Output
local multiply = add_tool("AlphaMultiply", "ReferenceMultiply", -1, 3)
multiply.Input = premult_cst.Output
comp:Unlock()

comp:Lock()
add_saver("SaveOpaqueActual", "/private/tmp/inputprep_opaque_actual.exr",
    first_output(opaque_processor), 0)
add_saver("SaveOpaqueReference", "/private/tmp/inputprep_opaque_reference.exr",
    opaque.Output, 1)
add_saver("SaveEmbeddedActual", "/private/tmp/inputprep_embedded_actual.exr",
    first_output(embedded_processor), 2)
add_saver("SaveEmbeddedReference", "/private/tmp/inputprep_embedded_reference.exr",
    multiply.Output, 3)
comp:Unlock()

local render_ok = comp:Render({ Start = 0, End = 0, Wait = true })
if render_ok == false then fail("Fusion render failed") end

print("INPUTPREP_COLOR_ALPHA_RENDER_OK")
return comp
