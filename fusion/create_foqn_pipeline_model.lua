-- Creates a persistent, end-to-end FOQN model composition using real E06/0010 paths.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local SCRIPT_DIR = script_directory()
local OUTPUT_PATH = SCRIPT_DIR .. "examples/FOQN_E06_0010_Pipeline_Model.comp"
local PLATE_PATH = "_FOQN:/FOQN_E06/BRUTOS/H264/FOQN_E06_0010.mov"

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end

local function is_empty_unsaved(comp)
    if comp == nil then return false end
    local attrs = comp:GetAttrs() or {}
    if tostring(attrs.COMPS_FileName or "") ~= "" then return false end
    for _ in pairs(comp:GetToolList(false) or {}) do return false end
    return true
end

local model = fusion.CurrentComp
local attrs = model and model:GetAttrs() or {}
if tostring(attrs.COMPS_FileName or "") == OUTPUT_PATH then
    model:Lock()
    for _, tool in pairs(model:GetToolList(false) or {}) do tool:Delete() end
    model:Unlock()
elseif model ~= nil and not is_empty_unsaved(model) then
    error("Close the active composition before creating the FOQN pipeline model")
elseif model == nil then
    model = fusion:NewComp()
end
if model == nil then error("Fusion could not create the FOQN pipeline model") end
_G.comp = model

model:SetPrefs({
    ["Comp.FrameFormat.Width"] = 1920,
    ["Comp.FrameFormat.Height"] = 1080,
    ["Comp.FrameFormat.AspectX"] = 1,
    ["Comp.FrameFormat.AspectY"] = 1,
})

local function add_tool(reg_id, name, x, y)
    local tool = model:AddTool(reg_id, x, y)
    if tool == nil then error("Fusion could not add " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    return tool
end

local function mark_managed(tool, role)
    tool:SetData("FOQNPipeline.Managed", true)
    tool:SetData("FOQNPipeline.Role", role)
    tool:SetData("FOQNPipeline.SchemaVersion", 1)
end

local function first_output(tool)
    for _, output in pairs(tool:GetOutputList() or {}) do return output end
    error((tool:GetAttrs().TOOLS_Name or "tool") .. " has no output")
end

local function set(tool, id, value)
    if tool[id] == nil then error("missing control " .. id) end
    tool[id][model.CurrentTime or 0] = value
end

model:Lock()
local plate = add_tool("Loader", "L_FOQN_E06_0010_Plate", -6, 0)
mark_managed(plate, "Loader")
plate.Clip[model.CurrentTime or 0] = PLATE_PATH
local wip_saver = add_tool("Saver", "S_FOQN_E06_0010_WIP", 3, -2)
local clean_saver = add_tool("Saver", "S_FOQN_E06_0010_Clean", 3, 2)
mark_managed(wip_saver, "Saver")
mark_managed(clean_saver, "Saver")
model:Unlock()

-- Input processing: the only transform path in the model.
_G.INPUTPREP_OVERRIDES = {
    sourceWidth = 1920,
    sourceHeight = 1080,
    workingWidth = 1920,
    workingHeight = 1080,
    cropRatio = 16 / 9,
    enableResize = false,
    enableCrop = false,
}
local input_builder = dofile(SCRIPT_DIR .. "input_prep/build_input_prep.lua")
_G.INPUTPREP_OVERRIDES = nil
local input_prep = input_builder.last_group
if input_prep == nil then error("InputPrep was not created") end
input_prep:SetAttrs({ TOOLS_Name = "L_InputPrep_FOQN_E06_0010" })
mark_managed(input_prep, "InputPrep")
input_prep.MainInput1 = plate.Output

-- Output processing: one WIP delivery and one clean delivery from the same prepared image.
local output_builder = dofile(SCRIPT_DIR .. "output_packager/build_output_packager.lua")
local wip_packager = output_builder.last_group
local clean_packager = output_builder.run(model)
if wip_packager == nil or clean_packager == nil then error("OutputPackager was not created") end
wip_packager:SetAttrs({ TOOLS_Name = "S_OutputPackager_FOQN_E06_0010_WIP" })
clean_packager:SetAttrs({ TOOLS_Name = "S_OutputPackager_FOQN_E06_0010_Clean" })
mark_managed(wip_packager, "OutputPackager")
mark_managed(clean_packager, "OutputPackager")
wip_packager.MainInput1 = first_output(input_prep)
clean_packager.MainInput1 = first_output(input_prep)
wip_saver.Input:ConnectTo(first_output(wip_packager))
clean_saver.Input:ConnectTo(first_output(clean_packager))
set(wip_packager, "OP_EnableReviewRaster", 1)
set(wip_packager, "OP_EnableWIP", 1)
set(clean_packager, "OP_EnableReviewRaster", 1)
set(clean_packager, "OP_EnableWIP", 0)

-- Shared shot metadata resolves the real FOQN source and output paths.
dofile(SCRIPT_DIR .. "shot_config/build_shot_config.lua")
local shot_apply = dofile(SCRIPT_DIR .. "shot_config/apply_shot_config.lua")
local shot_config, shot_error = shot_apply.find_config(model)
if shot_config == nil then error(shot_error or "ShotConfig was not created") end
shot_config:SetAttrs({ TOOLS_Name = "G_ShotConfig" })
set(shot_config, shot_apply.CONTROL.show, "FOQN")
set(shot_config, shot_apply.CONTROL.episode, "E06")
set(shot_config, shot_apply.CONTROL.shot, "0010")
set(shot_config, shot_apply.CONTROL.version, "v001")
set(shot_config, shot_apply.CONTROL.root, "_FOQN:")
set(shot_config, shot_apply.CONTROL.working_resolution, { 1920, 1080 })
set(shot_config, shot_apply.CONTROL.review_resolution, { 1920, 1080 })
set(shot_config, shot_apply.CONTROL.crop_ratio, 16 / 9)

local function set_shot_target(kind, index, node_name, template)
    local node_control, template_control = shot_apply.target_controls(kind, index)
    set(shot_config, node_control, node_name)
    set(shot_config, template_control, template)
end

set_shot_target("Loader", 1, "L_FOQN_E06_0010_Plate",
    "{root}/{show}_{episode}/BRUTOS/H264/{show}_{episode}_{shot}.mov")
set_shot_target("Saver", 1, "S_FOQN_E06_0010_WIP",
    "{root}/{show}_{episode}/WIP/{show}_{episode}_{shot}_WIP_{version}.mov")
set_shot_target("Saver", 2, "S_FOQN_E06_0010_Clean",
    "{root}/{show}_{episode}/RENDERS/{show}_{episode}_{shot}_GFX_{version}.mov")
local shot_ok, shot_apply_error = shot_apply.run(model)
if not shot_ok then error(shot_apply_error or "ShotConfig could not apply the FOQN model") end

local flow = model.CurrentFrame and model.CurrentFrame.FlowView
if flow ~= nil then
    pcall(function() flow:SetPos(plate, -6, 0) end)
    pcall(function() flow:SetPos(input_prep, -3, 0) end)
    pcall(function() flow:SetPos(wip_packager, 0, -2) end)
    pcall(function() flow:SetPos(clean_packager, 0, 2) end)
    pcall(function() flow:SetPos(wip_saver, 3, -2) end)
    pcall(function() flow:SetPos(clean_saver, 3, 2) end)
    pcall(function() flow:SetPos(shot_config, -6, 5) end)
end

model:SetActiveTool(shot_config)
local saved = model:Save(OUTPUT_PATH)
if saved == false then error("Fusion could not save the FOQN pipeline model") end
print("FOQN_PIPELINE_MODEL_READY: " .. OUTPUT_PATH)
return model
