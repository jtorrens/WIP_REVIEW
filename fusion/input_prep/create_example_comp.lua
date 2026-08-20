-- Creates the persistent visual InputPrep v0.1 example composition.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local SCRIPT_DIR = script_directory()
local OUTPUT_PATH = SCRIPT_DIR .. "examples/InputPrep_Example.comp"
local BUILD_PATH = SCRIPT_DIR .. "build_input_prep.lua"
local CONFIG_BUILD_PATH = SCRIPT_DIR .. "build_input_prep_config.lua"
local APPLY_PATH = SCRIPT_DIR .. "apply_input_prep.lua"
local SHOT_BUILD_PATH = SCRIPT_DIR .. "../shot_config/build_shot_config.lua"

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local example = fusion:NewComp()
if example == nil then error("Fusion could not create the InputPrep example comp") end
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

local function configure_corner_source(tool, width, height)
    tool.Width[0] = width
    tool.Height[0] = height
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
local source = add_tool("Background", "ExampleSource_2160x2160", -2, 0)
configure_corner_source(source, 2160, 2160)
example:Unlock()

_G.INPUTPREP_OVERRIDES = {
    sourceWidth = 2160,
    sourceHeight = 2160,
}
local builder = dofile(BUILD_PATH)
_G.INPUTPREP_OVERRIDES = nil
local input_prep = builder.last_group
if input_prep == nil then error("InputPrep was not created") end
input_prep.MainInput1 = source.Output

dofile(SHOT_BUILD_PATH)
dofile(CONFIG_BUILD_PATH)
local apply = dofile(APPLY_PATH)
local input_prep_config, config_error = apply.find_config(example)
if input_prep_config == nil then
    error(config_error or "InputPrepConfig was not created")
end
input_prep_config[apply.target_control(1)][example.CurrentTime] =
    input_prep:GetAttrs().TOOLS_Name
local applied, apply_error = apply.run(example)
if not applied then error(apply_error or "InputPrep example apply failed") end

local flow = example.CurrentFrame and example.CurrentFrame.FlowView
if flow ~= nil then
    pcall(function() flow:SetPos(source, -2, 0) end)
    pcall(function() flow:SetPos(input_prep, 0, 0) end)
    local shot_config = example:FindTool("ShotConfig")
    if shot_config ~= nil then pcall(function() flow:SetPos(shot_config, -2, 2) end) end
    pcall(function() flow:SetPos(input_prep_config, 0, 2) end)
end
example:SetActiveTool(input_prep_config)

local saved = example:Save(OUTPUT_PATH)
if saved == false then error("Fusion could not save the InputPrep example comp") end
print("INPUTPREP_EXAMPLE_READY: " .. OUTPUT_PATH)
return example
