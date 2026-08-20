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

local example = nil
for _ = 1, 30 do
    example = fusion:NewComp()
    if example ~= nil then break end
    bmd.wait(1)
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
local main_plate = add_tool("Loader", "MainPlate", -2, 0)
local phone_ui = add_tool("Loader", "PhoneUI", -2, 1)
local master_out = add_tool("Saver", "MasterOut", 2, 0)
local client_review = add_tool("Saver", "ClientReview", 2, 1)
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
set_target("Loader", 1, "MainPlate",
    "{root}/{show}_{episode}/BRUTOS/{show}_{episode}_{shot}.mov")
set_target("Loader", 2, "PhoneUI",
    "{root}/{show}_{episode}/SCREENS/{shot}.mov")
set_target("Saver", 1, "MasterOut",
    "{root}/{show}_{episode}/RENDERS/{show}_{episode}_{shot}_{version}.mov")
set_target("Saver", 2, "ClientReview",
    "{root}/{show}_{episode}/WIP/{show}_{episode}_{shot}_REF_{version}.mov")

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
