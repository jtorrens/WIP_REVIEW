-- Creates the persistent visual InputPrep v0.1 prototype composition.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local SCRIPT_DIR = script_directory()
local OUTPUT_PATH = SCRIPT_DIR .. "examples/InputPrep_Prototype.comp"
local BUILD_PATH = SCRIPT_DIR .. "build_input_prep.lua"

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local example = fusion:NewComp()
if example == nil then error("Fusion could not create the InputPrep prototype comp") end
_G.comp = example

example:SetPrefs({
    ["Comp.FrameFormat.Width"] = 3840,
    ["Comp.FrameFormat.Height"] = 2160,
    ["Comp.FrameFormat.AspectX"] = 1,
    ["Comp.FrameFormat.AspectY"] = 1,
})

local function add_tool(reg_id, name, x, y)
    local tool = example:AddTool(reg_id, x, y)
    if tool == nil then error("Fusion could not add " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    return tool
end

local function configure_corner_source(tool)
    tool.Width[0] = 3840
    tool.Height[0] = 2160
    tool.Type[0] = "Corner"
    tool.TopLeftRed[0] = 0.92
    tool.TopLeftGreen[0] = 0.12
    tool.TopLeftBlue[0] = 0.06
    tool.TopLeftAlpha[0] = 1
    tool.TopRightRed[0] = 0.08
    tool.TopRightGreen[0] = 0.55
    tool.TopRightBlue[0] = 0.95
    tool.TopRightAlpha[0] = 1
    tool.BottomLeftRed[0] = 0.12
    tool.BottomLeftGreen[0] = 0.82
    tool.BottomLeftBlue[0] = 0.22
    tool.BottomLeftAlpha[0] = 1
    tool.BottomRightRed[0] = 0.92
    tool.BottomRightGreen[0] = 0.72
    tool.BottomRightBlue[0] = 0.08
    tool.BottomRightAlpha[0] = 1
end

example:Lock()
local source = add_tool("Background", "PrototypeSource_3840x2160", -2, 0)
configure_corner_source(source)
example:Unlock()

local builder = dofile(BUILD_PATH)
local input_prep = builder.last_group
if input_prep == nil then error("InputPrep prototype was not created") end
input_prep.MainInput1 = source.Output

local flow = example.CurrentFrame and example.CurrentFrame.FlowView
if flow ~= nil then
    pcall(function() flow:SetPos(source, -2, 0) end)
    pcall(function() flow:SetPos(input_prep, 0, 0) end)
end
example:SetActiveTool(input_prep)

local saved = example:Save(OUTPUT_PATH)
if saved == false then error("Fusion could not save the InputPrep prototype comp") end
print("INPUTPREP_PROTOTYPE_READY: " .. OUTPUT_PATH)
return example
