-- Acceptance test for explicit targets and transactional InputPrep apply.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local TEST_DIR = script_directory()
local INPUT_PREP_DIR = TEST_DIR .. "../"
local SHOT_CONFIG_DIR = INPUT_PREP_DIR .. "../shot_config/"
local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local comp = fusion:NewComp()
if comp == nil then error("Fusion could not create the config test comp") end
_G.comp = comp

local function fail(message)
    error("InputPrep config acceptance failure: " .. message, 2)
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

local function add_source(name, width, height, x)
    local source = comp:AddTool("Background", x, -1)
    if source == nil then fail("unable to create " .. name) end
    source:SetAttrs({ TOOLS_Name = name })
    source.Width[0] = width
    source.Height[0] = height
    source.TopLeftRed[0] = 0.2
    source.TopLeftGreen[0] = 0.4
    source.TopLeftBlue[0] = 0.8
    return source
end

local function set(tool, id, value)
    if tool[id] == nil then fail(id .. " is missing") end
    tool[id][comp.CurrentTime] = value
end

local function get(tool, id)
    if tool[id] == nil then fail(id .. " is missing") end
    return tool[id][comp.CurrentTime]
end

comp:SetPrefs({
    ["Comp.FrameFormat.Width"] = 384,
    ["Comp.FrameFormat.Height"] = 216,
    ["Comp.FrameFormat.AspectX"] = 1,
    ["Comp.FrameFormat.AspectY"] = 1,
})

comp:Lock()
local square = add_source("ConfigSourceSquare", 400, 400, -3)
local wide = add_source("ConfigSourceWide", 600, 200, -2)
local ignored_source = add_source("ConfigSourceIgnored", 384, 216, -1)
comp:Unlock()

_G.INPUTPREP_OVERRIDES = { sourceWidth = 400, sourceHeight = 400 }
local builder = dofile(INPUT_PREP_DIR .. "build_input_prep.lua")
_G.INPUTPREP_OVERRIDES = nil
local first = builder.last_group
local second = builder.run(comp, { sourceWidth = 600, sourceHeight = 200 })
local ignored = builder.run(comp, { sourceWidth = 384, sourceHeight = 216 })
assert_true(first ~= nil and second ~= nil and ignored ~= nil,
    "InputPrep processors were not created")
first.MainInput1 = square.Output
second.MainInput1 = wide.Output
ignored.MainInput1 = ignored_source.Output

dofile(SHOT_CONFIG_DIR .. "build_shot_config.lua")
local shot_apply = dofile(SHOT_CONFIG_DIR .. "apply_shot_config.lua")
local shot, shot_error = shot_apply.find_config(comp)
assert_true(shot ~= nil, shot_error or "ShotConfig not found")
set(shot, "SC_WorkingResolution", { 384, 216 })
set(shot, "SC_CropRatio", 2.0)

dofile(INPUT_PREP_DIR .. "build_input_prep_config.lua")
local apply = dofile(INPUT_PREP_DIR .. "apply_input_prep.lua")
local config, config_error = apply.find_config(comp)
assert_true(config ~= nil, config_error or "InputPrepConfig not found")
set(config, apply.target_control(1), first:GetAttrs().TOOLS_Name)
set(config, apply.target_control(2), second:GetAttrs().TOOLS_Name)

local ok, apply_error = apply.run(comp)
assert_true(ok, apply_error or "apply failed")
assert_equal(get(first, "IP_ResizeWidth"), 384, "square fill width")
assert_equal(get(second, "IP_ResizeWidth"), 648, "wide fill width")
assert_equal(get(first, "IP_ResizeHeight"), 216, "working height")
assert_equal(get(first, "IP_CropWidth"), 384, "crop width")
assert_equal(get(first, "IP_CropHeight"), 192, "crop height")
assert_equal(get(first, "IP_EnableResize"), 1, "square resize enabled")
assert_equal(get(first, "IP_EnableCrop"), 1, "square crop enabled")
assert_equal(get(first, "IP_EnableColor"), 1, "color enabled")
assert_equal(get(first, "IP_EmbeddedAlpha"), 0, "opaque alpha policy")
assert_equal(get(first, "IP_SourceColorSpace"), "REC709_COLORSPACE",
    "source color-space ID")
assert_equal(get(first, "IP_SourceGamma"), "TWOPOINTFOUR_GAMMA",
    "source gamma ID")
assert_equal(get(first, "IP_WorkingGamma"), "LINEAR_GAMMA",
    "working gamma ID")
assert_equal(get(ignored, "IP_ResizeWidth"), 3840,
    "unregistered InputPrep must remain unchanged")
assert_equal(get(config, apply.CONTROL.status), "Applied 2 InputPrep target(s).",
    "success status")

set(shot, "SC_WorkingResolution", { 512, 288 })
ok, apply_error = apply.run(comp)
assert_true(ok, apply_error or "resolution update failed")
assert_equal(get(first, "IP_ResizeWidth"), 512,
    "updated square fill width")
assert_equal(get(second, "IP_ResizeWidth"), 864,
    "updated wide fill width")
assert_equal(get(first, "IP_CropWidth"), 512, "updated crop width")
assert_equal(get(first, "IP_CropHeight"), 256, "updated crop height")

local config_builder = dofile(INPUT_PREP_DIR .. "build_input_prep_config.lua")
config, config_error = apply.find_config(comp)
assert_true(config ~= nil, config_error or "rebuilt InputPrepConfig not found")
assert_equal(get(config, apply.target_control(1)), first:GetAttrs().TOOLS_Name,
    "first target preserved on rebuild")
assert_equal(get(config, apply.target_control(2)), second:GetAttrs().TOOLS_Name,
    "second target preserved on rebuild")

set(first, "IP_ResizeWidth", 1234)
set(config, apply.target_control(2), "MissingInputPrep")
ok, apply_error = apply.run(comp)
assert_true(not ok and tostring(apply_error):find("target not found", 1, true),
    "missing target must abort validation")
assert_equal(get(first, "IP_ResizeWidth"), 1234,
    "validation failure changed a valid target")

set(config, apply.target_control(2), first:GetAttrs().TOOLS_Name)
ok, apply_error = apply.run(comp)
assert_true(not ok and tostring(apply_error):find("duplicate target", 1, true),
    "duplicate target must abort validation")

set(config, apply.target_control(2), second:GetAttrs().TOOLS_Name)
set(first, "IP_ResizeWidth", 777)
set(second, "IP_ResizeWidth", 888)
local prepared, prepare_error = apply.prepare(comp, config, comp.CurrentTime)
assert_true(prepared ~= nil, prepare_error or "rollback preparation failed")
prepared[2].values.IP_ResizeWidth = function() return 0 end
ok, apply_error = apply.apply_prepared(comp, prepared, comp.CurrentTime)
assert_true(not ok, "forced write failure did not abort")
assert_equal(get(first, "IP_ResizeWidth"), 777,
    "rollback did not restore first target")
assert_equal(get(second, "IP_ResizeWidth"), 888,
    "rollback did not restore failing target")

print("INPUTPREP_CONFIG_APPLY_HOST_TEST_OK")
return comp
