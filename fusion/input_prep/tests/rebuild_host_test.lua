-- Rebuild acceptance: identity, values, registry and external connections.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local TEST_DIR = script_directory()
local INPUT_PREP_DIR = TEST_DIR .. "../"
local OUTPUT_PATH = "/private/tmp/InputPrep_Rebuild_Test.comp"
local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local comp = fusion:NewComp()
if comp == nil then error("Fusion could not create the rebuild test comp") end
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local function fail(message)
    error("InputPrep rebuild failure: " .. message, 2)
end

local function assert_equal(actual, expected, label)
    if tostring(actual) ~= tostring(expected) then
        fail(string.format("%s: expected %s, got %s",
            label, tostring(expected), tostring(actual)))
    end
end

local function assert_true(value, label)
    if not value then fail(label) end
end

local function first_output(tool)
    for _, output in pairs(tool:GetOutputList() or {}) do return output end
    fail("tool has no output")
end

local source = comp:AddTool("Background", -2, 0)
source:SetAttrs({ TOOLS_Name = "RebuildSource" })
source.Width[0] = 32
source.Height[0] = 32

_G.INPUTPREP_OVERRIDES = { sourceWidth = 32, sourceHeight = 32 }
local builder = dofile(INPUT_PREP_DIR .. "build_input_prep.lua")
_G.INPUTPREP_OVERRIDES = nil
local original = builder.last_group
assert_true(original ~= nil, "original InputPrep was not created")
local original_name = original:GetAttrs().TOOLS_Name
original.MainInput1 = source.Output
original.IP_EnableDepth[0] = 0
original.IP_EnableColor[0] = 0
original.IP_EnableResize[0] = 0
original.IP_EnableCrop[0] = 0
original.IP_EmbeddedAlpha[0] = 1
original.IP_ResizeWidth[0] = 648
original.IP_Status[0] = "Preserved status"

local downstream = comp:AddTool("BrightnessContrast", 2, 0)
downstream:SetAttrs({ TOOLS_Name = "RebuildConsumer" })
downstream.Input = first_output(original)

dofile(INPUT_PREP_DIR .. "build_input_prep_config.lua")
local apply = dofile(INPUT_PREP_DIR .. "apply_input_prep.lua")
local config, config_error = apply.find_config(comp)
assert_true(config ~= nil, config_error or "InputPrepConfig was not created")
config[apply.target_control(1)][0] = original_name

_G.tool = original
local rebuild_builder = dofile(INPUT_PREP_DIR .. "build_input_prep.lua")
_G.tool = nil
local replacement = rebuild_builder.last_group
assert_true(replacement ~= nil, "rebuild returned no processor")
assert_true(replacement ~= original, "rebuild returned the previous processor")
assert_equal(replacement:GetAttrs().TOOLS_Name, original_name,
    "processor name")
assert_equal(replacement:GetData("InputPrep.Role"), "InputPrep", "role")
assert_equal(tonumber(replacement:GetData("InputPrep.SchemaVersion")), 1,
    "schema")
assert_equal(replacement.IP_EnableDepth[0], 0, "depth bypass")
assert_equal(replacement.IP_EnableColor[0], 0, "color bypass")
assert_equal(replacement.IP_EnableResize[0], 0, "resize bypass")
assert_equal(replacement.IP_EnableCrop[0], 0, "crop bypass")
assert_equal(replacement.IP_EmbeddedAlpha[0], 1, "alpha policy")
assert_equal(replacement.IP_ResizeWidth[0], 648, "applied resize width")
assert_equal(replacement.IP_Status[0], "Preserved status", "status")
assert_true(replacement.MainInput1:GetConnectedOutput() ~= nil,
    "input connection was not restored")
assert_true(downstream.Input:GetConnectedOutput() ~= nil,
    "output consumer was not restored")
assert_equal(config[apply.target_control(1)][0], original_name,
    "registry target")

local processors = 0
for _, tool in pairs(comp:GetToolList(false) or {}) do
    if tool:GetData("InputPrep.Role") == "InputPrep" then processors = processors + 1 end
end
assert_equal(processors, 1, "processor count")
local rendered = downstream.Output[0]
assert_true(rendered ~= nil and tonumber(rendered.Width) == 32 and
    tonumber(rendered.Height) == 32,
    "rebuilt output did not render through its consumer")

local saved = comp:Save(OUTPUT_PATH)
assert_true(saved ~= false, "rebuild test comp was not saved")
print("INPUTPREP_REBUILD_HOST_TEST_OK")
return comp
