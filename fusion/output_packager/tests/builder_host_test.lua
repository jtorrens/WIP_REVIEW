-- Fusion 21 host test for the OutputPackager image component.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local comp = fusion:NewComp()
if comp == nil then error("Fusion could not create the builder test comp") end
_G.comp = comp

local function fail(message)
    error("OutputPackager builder failure: " .. message, 2)
end

local source = comp:AddTool("Background", -2, 0)
source:SetAttrs({ TOOLS_Name = "OutputPackagerBuilder_Source" })
source.Width[0] = 400
source.Height[0] = 200

_G.OUTPUTPACKAGER_OVERRIDES = { reviewWidth = 192, reviewHeight = 108 }
local builder = dofile(script_directory() .. "../build_output_packager.lua")
_G.OUTPUTPACKAGER_OVERRIDES = nil
local group = builder.last_group
if group == nil then fail("builder returned no group") end
if group:GetData("OutputPackager.Role") ~= "OutputPackager" then
    fail("Role metadata is missing")
end
if tonumber(group:GetData("OutputPackager.SchemaVersion")) ~= 1 then
    fail("SchemaVersion metadata is missing")
end
local connected = group.MainInput1:ConnectTo(source.Output)
if connected == false then fail("unable to connect source to group") end

local group_output = nil
for _, output in pairs(group:GetOutputList() or {}) do
    group_output = output
    break
end
if group_output == nil then fail("group has no output") end
local observer = comp:AddTool("PipeRouter", 2, 0)
observer:SetAttrs({ TOOLS_Name = "OutputPackagerBuilder_Observer" })
connected = observer.Input:ConnectTo(group_output)
if connected == false then fail("unable to connect group to observer") end

local function output_dimensions()
    local image = observer.Output[comp.CurrentTime]
    if image == nil then fail("group consumer returned no image") end
    return tonumber(image.Width), tonumber(image.Height)
end

group.OP_EnableReviewRaster[0] = 0
group.OP_EnableWIP[0] = 0
local width, height = output_dimensions()
if width ~= 400 or height ~= 200 then fail("source-raster bypass is wrong") end

group.OP_EnableReviewRaster[0] = 1
width, height = output_dimensions()
if width ~= 192 or height ~= 108 then fail("review raster is wrong") end

group.OP_EnableWIP[0] = 1
width, height = output_dimensions()
if width ~= 192 or height ~= 108 then fail("WIP output raster is wrong") end

local invalid_ok = pcall(function()
    builder.run(comp, { enableReviewRaster = false, enableWIP = true })
end)
if invalid_ok then fail("WIP without Review Raster was accepted") end
if comp:FindTool("OutputPackager2") ~= nil then
    fail("invalid build left a partial group")
end

print("OUTPUTPACKAGER_BUILDER_HOST_TEST_OK")
return comp
