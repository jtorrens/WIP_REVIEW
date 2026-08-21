-- Creates and saves the persistent ShotConfig example composition.
-- Run through Fusion's fuscript interpreter or from Fusion Console.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local SCRIPT_DIR = script_directory()
local OUTPUT_PATH = SCRIPT_DIR .. "examples/ShotConfig_Example.comp"
local BUILD_PATH = SCRIPT_DIR .. "build_shot_config.lua"
local APPLY_PATH = SCRIPT_DIR .. "apply_shot_config.lua"

local fusion = nil
for _ = 1, 60 do
    fusion = bmd.scriptapp("Fusion", "localhost")
    if fusion ~= nil then break end
    bmd.wait(1)
end
if fusion == nil then error("Fusion Standalone is not reachable") end

local function is_empty_unsaved(comp)
    if comp == nil then return false end
    local attrs = comp:GetAttrs() or {}
    if tostring(attrs.COMPS_FileName or "") ~= "" then return false end
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
    error("Close the active composition before creating the ShotConfig example")
elseif example == nil then
    for _ = 1, 30 do
        example = fusion:NewComp()
        if example ~= nil then break end
        bmd.wait(1)
    end
end
if example == nil then error("Fusion could not create the example composition") end

local function add_tool(reg_id, name, x, y)
    local tool = example:AddTool(reg_id, x, y)
    if tool == nil then error("unable to add " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    return tool
end

_G.comp = example
example:Lock()
local main_plate = add_tool("Loader", "Loader_MainPlate", -2, 0)
local phone_ui = add_tool("Loader", "Loader_PhoneUI", -2, 1)
local master_out = add_tool("Saver", "Saver_MasterOut", 2, 0)
local client_review = add_tool("Saver", "Saver_ClientReview", 2, 1)
master_out.Input = main_plate.Output
client_review.Input = phone_ui.Output
example:Unlock()

dofile(BUILD_PATH)
local apply = dofile(APPLY_PATH)
local config, find_error = apply.find_config(example)
if config == nil then error(find_error or "ShotConfig was not created") end

local time = example.CurrentTime or 0
local function write(control, value)
    local input = config[control]
    if input == nil then error("ShotConfig control is missing: " .. control) end
    input[time] = value
end
local function set_target(kind, index, node_name, template)
    local node_control, template_control = apply.target_controls(kind, index)
    write(node_control, node_name)
    write(template_control, template)
end

write(apply.CONTROL.root, "_SHOTCONFIG_TEST:")
set_target("Loader", 1, "Loader_MainPlate",
    "{root}/{show}_{episode}/BRUTOS/{show}_{episode}_{shot}.mov")
set_target("Loader", 2, "Loader_PhoneUI",
    "{root}/{show}_{episode}/SCREENS/{shot}.mov")
set_target("Saver", 1, "Saver_MasterOut",
    "{root}/{show}_{episode}/RENDERS/{show}_{episode}_{shot}_{version}.mov")
set_target("Saver", 2, "Saver_ClientReview",
    "{root}/{show}_{episode}/WIP/{show}_{episode}_{shot}_REF_{version}.mov")

local refresh_previews = dofile(SCRIPT_DIR .. "refresh_resolved_paths.lua")
local previews_ok, preview_error = refresh_previews.run(config)
if not previews_ok then error(preview_error or "ShotConfig previews could not refresh") end
local applied, apply_error = apply.run(example)
if not applied then error(apply_error or "unable to apply the example targets") end

example:SetActiveTool(config)
local saved = example:Save(OUTPUT_PATH)
local file = io.open(OUTPUT_PATH, "rb")
if file == nil then error("Fusion did not save the example composition") end
file:close()
if saved == false then error("Fusion rejected the example composition path") end

print("SHOTCONFIG_EXAMPLE_COMP_READY: " .. OUTPUT_PATH)
return example
