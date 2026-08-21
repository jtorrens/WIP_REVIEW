-- Acceptance test for explicit OutputPackager/Saver pairs and rollback.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local TEST_DIR = script_directory()
local MODULE_DIR = TEST_DIR .. "../"
local SHOT_CONFIG_DIR = MODULE_DIR .. "../shot_config/"
local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local temp_comp = dofile(TEST_DIR .. "../../test_support/temp_comp.lua")
local comp = temp_comp.acquire(fusion)
if comp == nil then error("Fusion could not create the config apply test comp") end
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local function fail(message)
    error("OutputPackager config acceptance failure: " .. message, 2)
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

local function set(tool, id, value)
    if tool[id] == nil then fail(id .. " is missing") end
    tool[id][comp.CurrentTime] = value
end

local function get(tool, id)
    if tool[id] == nil then fail(id .. " is missing") end
    return tool[id][comp.CurrentTime]
end

local function first_output(tool)
    for _, output in pairs(tool:GetOutputList() or {}) do return output end
    fail((tool:GetAttrs().TOOLS_Name or "tool") .. " has no output")
end

local function add_source(name, x)
    local source = comp:AddTool("Background", x, -1)
    if source == nil then fail("unable to add " .. name) end
    source:SetAttrs({ TOOLS_Name = name })
    source.Width[0] = 400
    source.Height[0] = 200
    return source
end

local function add_saver(name, packager, x)
    local saver = comp:AddTool("Saver", x, 1)
    if saver == nil then fail("unable to add " .. name) end
    saver:SetAttrs({ TOOLS_Name = name })
    saver.Clip[0] = "/private/tmp/" .. name .. ".exr"
    local connected = saver.Input:ConnectTo(first_output(packager))
    if connected == false then fail("unable to connect " .. name) end
    return saver
end

comp:Lock()
local source_a = add_source("PackageSourceA", -5)
local source_b = add_source("PackageSourceB", -4)
local source_ignored = add_source("PackageSourceIgnored", -3)
comp:Unlock()

_G.OUTPUTPACKAGER_OVERRIDES = { reviewWidth = 192, reviewHeight = 108 }
local builder = dofile(MODULE_DIR .. "build_output_packager.lua")
_G.OUTPUTPACKAGER_OVERRIDES = nil
local first = builder.last_group
local second = builder.run(comp, { reviewWidth = 192, reviewHeight = 108 })
local ignored = builder.run(comp, { reviewWidth = 192, reviewHeight = 108 })
first.MainInput1 = source_a.Output
second.MainInput1 = source_b.Output
ignored.MainInput1 = source_ignored.Output

comp:Lock()
local first_saver = add_saver("PackageSaverA", first, 1)
local second_saver = add_saver("PackageSaverB", second, 2)
add_saver("PackageSaverIgnored", ignored, 3)
comp:Unlock()

dofile(SHOT_CONFIG_DIR .. "build_shot_config.lua")
local shot_apply = dofile(SHOT_CONFIG_DIR .. "apply_shot_config.lua")
local shot, shot_error = shot_apply.find_config(comp)
assert_true(shot ~= nil, shot_error or "ShotConfig not found")
set(shot, "SC_ReviewResolution", { 320, 180 })
set(shot, "SC_CropAspectWidth", 2.39)
set(shot, "SC_CropAspectHeight", 1)

dofile(MODULE_DIR .. "build_output_packager_config.lua")
local apply = dofile(MODULE_DIR .. "apply_output_packager.lua")
local config, config_error = apply.find_config(comp)
assert_true(config ~= nil, config_error or "OutputPackagerConfig not found")

local function configure_row(index, packager, saver, enabled, review, wip)
    set(config, apply.target_control(index, "packager"),
        packager and packager:GetAttrs().TOOLS_Name or "")
    set(config, apply.target_control(index, "saver"),
        saver and saver:GetAttrs().TOOLS_Name or "")
    set(config, apply.target_control(index, "enabled"), enabled and 1 or 0)
    set(config, apply.target_control(index, "review"), review and 1 or 0)
    set(config, apply.target_control(index, "wip"), wip and 1 or 0)
end

configure_row(1, first, first_saver, true, true, true)
configure_row(2, second, second_saver, false, true, false)

local ok, apply_error = apply.run(comp)
assert_true(ok, apply_error or "apply failed")
for _, packager in ipairs({ first, second }) do
    assert_equal(get(packager, "OP_ReviewWidth"), 320, "review width")
    assert_equal(get(packager, "OP_ReviewHeight"), 180, "review height")
    assert_equal(get(packager, "OP_CropRatio"), 2.39, "crop ratio")
end
assert_equal(get(first, "OP_EnableWIP"), 1, "first WIP state")
assert_equal(get(second, "OP_EnableWIP"), 0, "second WIP state")
assert_true(first_saver:GetAttrs().TOOLB_PassThrough ~= true,
    "enabled Saver was disabled")
assert_true(second_saver:GetAttrs().TOOLB_PassThrough == true,
    "disabled Saver was enabled")
assert_equal(get(ignored, "OP_ReviewWidth"), 192,
    "unregistered package changed")
assert_equal(get(config, apply.CONTROL.status), "Applied 2 output package(s).",
    "success status")

dofile(MODULE_DIR .. "build_output_packager_config.lua")
config, config_error = apply.find_config(comp)
assert_true(config ~= nil, config_error or "rebuilt config not found")
assert_equal(get(config, apply.target_control(1, "packager")),
    first:GetAttrs().TOOLS_Name, "first packager preserved")
assert_equal(get(config, apply.target_control(2, "saver")),
    second_saver:GetAttrs().TOOLS_Name, "second Saver preserved")
assert_equal(get(config, apply.target_control(2, "enabled")), 0,
    "disabled state preserved")

set(first, "OP_ReviewWidth", 777)
set(config, apply.target_control(3, "packager"), first:GetAttrs().TOOLS_Name)
ok, apply_error = apply.run(comp)
assert_true(not ok and tostring(apply_error):find("both Packager and Saver", 1, true),
    "partial pair must abort")
assert_equal(get(first, "OP_ReviewWidth"), 777,
    "partial-pair validation changed a valid target")
set(config, apply.target_control(3, "packager"), "")

set(config, apply.target_control(1, "review"), 0)
set(config, apply.target_control(1, "wip"), 1)
ok, apply_error = apply.run(comp)
assert_true(not ok and tostring(apply_error):find("requires Review Raster", 1, true),
    "WIP without Review Raster must abort")
set(config, apply.target_control(1, "review"), 1)

local original_second_output = first_output(second)
second_saver.Input:ConnectTo(source_b.Output)
ok, apply_error = apply.run(comp)
assert_true(not ok and tostring(apply_error):find("different source", 1, true),
    "wrong Saver connection must abort")
second_saver.Input:ConnectTo(original_second_output)

set(first, "OP_ReviewWidth", 701)
set(second, "OP_ReviewWidth", 702)
first_saver:SetAttrs({ TOOLB_PassThrough = true })
second_saver:SetAttrs({ TOOLB_PassThrough = false })
local prepared, prepare_error = apply.prepare(comp, config, comp.CurrentTime)
assert_true(prepared ~= nil, prepare_error or "rollback preparation failed")
prepared[2].values.OP_ReviewWidth = function() return 0 end
ok, apply_error = apply.apply_prepared(comp, prepared, comp.CurrentTime)
assert_true(not ok, "forced write failure did not abort")
assert_equal(get(first, "OP_ReviewWidth"), 701,
    "rollback did not restore first package")
assert_equal(get(second, "OP_ReviewWidth"), 702,
    "rollback did not restore second package")
assert_true(first_saver:GetAttrs().TOOLB_PassThrough == true,
    "rollback did not restore first Saver")
assert_true(second_saver:GetAttrs().TOOLB_PassThrough ~= true,
    "rollback did not restore second Saver")

print("OUTPUTPACKAGER_CONFIG_APPLY_HOST_TEST_OK")
return comp
