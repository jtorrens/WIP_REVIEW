-- Fusion 21 rebuild test for OutputPackager values and connections.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local temp_comp = dofile(script_directory() .. "../../test_support/temp_comp.lua")
local comp = temp_comp.acquire(fusion)
if comp == nil then error("Fusion could not create the rebuild test comp") end
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local function fail(message)
    error("OutputPackager rebuild failure: " .. message, 2)
end

local function assert_equal(actual, expected, label)
    if tostring(actual) ~= tostring(expected) then
        fail(string.format("%s: expected %s, got %s",
            label, tostring(expected), tostring(actual)))
    end
end

local function first_output(tool)
    for _, output in pairs(tool:GetOutputList() or {}) do return output end
    fail("tool has no output")
end

local source = comp:AddTool("Background", -2, 0)
source:SetAttrs({ TOOLS_Name = "RebuildPackageSource" })
source.Width[0] = 400
source.Height[0] = 200

_G.OUTPUTPACKAGER_OVERRIDES = { reviewWidth = 192, reviewHeight = 108 }
local builder = dofile(script_directory() .. "../build_output_packager.lua")
_G.OUTPUTPACKAGER_OVERRIDES = nil
local original = builder.last_group
original.MainInput1 = source.Output
original.OP_EnableReviewRaster[0] = 1
original.OP_EnableWIP[0] = 1
original.OP_ReviewWidth[0] = 320
original.OP_ReviewHeight[0] = 180
original.OP_CropRatio[0] = 2.39
original.OP_Status[0] = "Rebuild sentinel"
local original_name = original:GetAttrs().TOOLS_Name

comp:Lock()
local saver = comp:AddTool("Saver", 2, 0)
saver:SetAttrs({ TOOLS_Name = "RebuildPackageSaver" })
saver.Clip[0] = "/private/tmp/outputpackager_rebuild.exr"
saver.Input:ConnectTo(first_output(original))
comp:Unlock()

local replacement = builder.rebuild(comp, original)
if replacement == nil then fail("builder returned no replacement") end
assert_equal(replacement:GetAttrs().TOOLS_Name, original_name, "name")
assert_equal(replacement:GetData("OutputPackager.Role"), "OutputPackager", "Role")
assert_equal(replacement:GetData("OutputPackager.SchemaVersion"), 1, "schema")
assert_equal(replacement.OP_EnableReviewRaster[0], 1, "review state")
assert_equal(replacement.OP_EnableWIP[0], 1, "WIP state")
assert_equal(replacement.OP_ReviewWidth[0], 320, "review width")
assert_equal(replacement.OP_ReviewHeight[0], 180, "review height")
assert_equal(replacement.OP_CropRatio[0], 2.39, "crop ratio")
assert_equal(replacement.OP_Status[0], "Rebuild sentinel", "status")
if replacement.MainInput1:GetConnectedOutput() == nil then
    fail("input connection was not restored")
end

local consumers = first_output(replacement):GetConnectedInputs() or {}
local saver_connected = false
for _, consumer in pairs(consumers) do
    if consumer == saver.Input or tostring(consumer) == tostring(saver.Input) then
        saver_connected = true
    end
end
if not saver_connected then fail("Saver connection was not restored") end

local role_count = 0
for _, candidate in pairs(comp:GetToolList(false) or {}) do
    if candidate:GetData("OutputPackager.Role") == "OutputPackager" then
        role_count = role_count + 1
    end
end
assert_equal(role_count, 1, "unique OutputPackager")

print("OUTPUTPACKAGER_REBUILD_HOST_TEST_OK")
return comp
