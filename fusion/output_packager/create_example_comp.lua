-- Creates the persistent OutputPackager v0.1 example composition.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local SCRIPT_DIR = script_directory()
local OUTPUT_PATH = SCRIPT_DIR .. "examples/OutputPackager_Example.comp"
local BUILD_PATH = SCRIPT_DIR .. "build_output_packager.lua"
local CONFIG_BUILD_PATH = SCRIPT_DIR .. "build_output_packager_config.lua"
local APPLY_PATH = SCRIPT_DIR .. "apply_output_packager.lua"
local SHOT_BUILD_PATH = SCRIPT_DIR .. "../shot_config/build_shot_config.lua"
local SHOT_APPLY_PATH = SCRIPT_DIR .. "../shot_config/apply_shot_config.lua"

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end

local function is_empty_unsaved(comp)
    if comp == nil then return false end
    local attrs = comp:GetAttrs() or {}
    local path = tostring(attrs.COMPS_FileName or "")
    if path ~= "" then return false end
    for _ in pairs(comp:GetToolList(false) or {}) do return false end
    return true
end

local example = fusion.CurrentComp
local example_attrs = example and example:GetAttrs() or {}
local example_path = tostring(example_attrs.COMPS_FileName or "")
if example_path == OUTPUT_PATH then
    example:Lock()
    for _, tool in pairs(example:GetToolList(false) or {}) do tool:Delete() end
    example:Unlock()
elseif example ~= nil and not is_empty_unsaved(example) then
    error("Close the active composition before creating the OutputPackager example")
elseif example == nil then
    example = fusion:NewComp()
end
if example == nil then error("Fusion could not create the OutputPackager example comp") end
_G.comp = example

example:SetPrefs({
    ["Comp.FrameFormat.Width"] = 3840,
    ["Comp.FrameFormat.Height"] = 1920,
    ["Comp.FrameFormat.AspectX"] = 1,
    ["Comp.FrameFormat.AspectY"] = 1,
})

local function add_tool(reg_id, name, x, y)
    local tool = example:AddTool(reg_id, x, y)
    if tool == nil then error("Fusion could not add " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    return tool
end

local function first_output(tool)
    for _, output in pairs(tool:GetOutputList() or {}) do return output end
    error((tool:GetAttrs().TOOLS_Name or "tool") .. " has no output")
end

example:Lock()
local source = add_tool("Background", "Background_ExampleFinalImage_2_00", -4, 0)
source.Width[0] = 3840
source.Height[0] = 1920
source.Type[0] = "Corner"
source.TopLeftRed[0] = 0.9
source.TopLeftGreen[0] = 0.12
source.TopLeftBlue[0] = 0.08
source.TopRightRed[0] = 0.08
source.TopRightGreen[0] = 0.45
source.TopRightBlue[0] = 0.95
source.BottomLeftRed[0] = 0.1
source.BottomLeftGreen[0] = 0.8
source.BottomLeftBlue[0] = 0.2
source.BottomRightRed[0] = 0.95
source.BottomRightGreen[0] = 0.7
source.BottomRightBlue[0] = 0.05
example:Unlock()

local builder = dofile(BUILD_PATH)
local wip_package = builder.last_group
local clean_package = builder.run(example)
wip_package:SetAttrs({ TOOLS_Name = "Group_OutputPackager_ClientReview" })
clean_package:SetAttrs({ TOOLS_Name = "Group_OutputPackager_CleanReview" })
wip_package.MainInput1 = source.Output
clean_package.MainInput1 = source.Output

wip_package.OP_TLEnabled[0] = 1
wip_package.OP_TLText[0] = "WIP · {frame}"
wip_package.OP_BREnabled[0] = 1
wip_package.OP_BRText[0] = "{timecode}"
wip_package.OP_FrameStart[0] = 1001
wip_package.OP_FPSMode[0] = 1
wip_package.OP_FPSOverride[0] = 24

example:Lock()
local wip_saver = add_tool("Saver", "Saver_ClientReview", 3, -1)
local clean_saver = add_tool("Saver", "Saver_CleanReview", 3, 1)
wip_saver.Input:ConnectTo(first_output(wip_package))
clean_saver.Input:ConnectTo(first_output(clean_package))
example:Unlock()

dofile(SHOT_BUILD_PATH)
local shot_apply = dofile(SHOT_APPLY_PATH)
local shot_config, shot_error = shot_apply.find_config(example)
if shot_config == nil then error(shot_error or "ShotConfig was not created") end
local time = example.CurrentTime or 0
local function set(tool, id, value)
    if tool[id] == nil then error("missing control " .. id) end
    tool[id][time] = value
end
set(shot_config, shot_apply.CONTROL.root, "_OUTPUTPACKAGER_TEST:")
set(shot_config, shot_apply.CONTROL.review_resolution, { 1920, 1080 })
set(shot_config, shot_apply.CONTROL.crop_aspect_width, 2)
set(shot_config, shot_apply.CONTROL.crop_aspect_height, 1)
local saver_node, saver_template = shot_apply.target_controls("Saver", 1)
set(shot_config, saver_node, "Saver_ClientReview")
set(shot_config, saver_template,
    "{root}/{show}_{episode}/WIP/{show}_{episode}_{shot}_WIP_{version}.mov")
saver_node, saver_template = shot_apply.target_controls("Saver", 2)
set(shot_config, saver_node, "Saver_CleanReview")
set(shot_config, saver_template,
    "{root}/{show}_{episode}/REVIEW/{show}_{episode}_{shot}_CLEAN_{version}.mov")
for index = 3, shot_apply.TARGET_SLOT_COUNT do
    local node_control, template_control = shot_apply.target_controls("Saver", index)
    set(shot_config, node_control, "")
    set(shot_config, template_control, "")
end
local refresh_previews = dofile(SCRIPT_DIR .. "../shot_config/refresh_resolved_paths.lua")
local previews_ok, preview_error = refresh_previews.run(shot_config)
if not previews_ok then error(preview_error or "ShotConfig previews could not refresh") end
local shot_ok, shot_apply_error = shot_apply.run(example)
if not shot_ok then error(shot_apply_error or "ShotConfig example apply failed") end

dofile(CONFIG_BUILD_PATH)
local apply = dofile(APPLY_PATH)
local config, config_error = apply.find_config(example)
if config == nil then error(config_error or "OutputPackagerConfig was not created") end

local function configure_row(index, packager_name, saver_name, enabled, review, wip)
    set(config, apply.target_control(index, "packager"), packager_name)
    set(config, apply.target_control(index, "saver"), saver_name)
    set(config, apply.target_control(index, "enabled"), enabled and 1 or 0)
    set(config, apply.target_control(index, "review"), review and 1 or 0)
    set(config, apply.target_control(index, "wip"), wip and 1 or 0)
end
configure_row(1, "Group_OutputPackager_ClientReview", "Saver_ClientReview",
    true, true, true)
configure_row(2, "Group_OutputPackager_CleanReview", "Saver_CleanReview",
    true, true, false)
local applied, apply_error = apply.run(example)
if not applied then error(apply_error or "OutputPackager example apply failed") end

local flow = example.CurrentFrame and example.CurrentFrame.FlowView
if flow ~= nil then
    pcall(function() flow:SetPos(source, -4, 0) end)
    pcall(function() flow:SetPos(wip_package, -1, -1) end)
    pcall(function() flow:SetPos(clean_package, -1, 1) end)
    pcall(function() flow:SetPos(wip_saver, 2, -1) end)
    pcall(function() flow:SetPos(clean_saver, 2, 1) end)
    pcall(function() flow:SetPos(shot_config, -4, 3) end)
    pcall(function() flow:SetPos(config, -1, 3) end)
end
example:SetActiveTool(config)

local saved = example:Save(OUTPUT_PATH)
if saved == false then error("Fusion could not save the OutputPackager example comp") end
print("OUTPUTPACKAGER_EXAMPLE_READY: " .. OUTPUT_PATH)
return example
