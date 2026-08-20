-- Runtime validation for the persistent OutputPackager example comp.

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local comp = fusion.CurrentComp
if comp == nil then error("OutputPackager example comp is not open") end

local function fail(message)
    error("OutputPackager example failure: " .. message, 2)
end

local wip = comp:FindTool("ClientReviewPackager")
local clean = comp:FindTool("CleanReviewPackager")
local wip_saver = comp:FindTool("ClientReviewSaver")
local clean_saver = comp:FindTool("CleanReviewSaver")
local config = comp:FindTool("OutputPackagerConfig")
if wip == nil or clean == nil or wip_saver == nil or clean_saver == nil or config == nil then
    fail("required example tools are missing")
end
if wip:GetData("OutputPackager.Role") ~= "OutputPackager" or
    clean:GetData("OutputPackager.Role") ~= "OutputPackager" then
    fail("package Role metadata is missing")
end
if tonumber(wip.OP_EnableReviewRaster[comp.CurrentTime]) ~= 1 or
    tonumber(wip.OP_EnableWIP[comp.CurrentTime]) ~= 1 then
    fail("WIP package switches are wrong")
end
if tonumber(clean.OP_EnableReviewRaster[comp.CurrentTime]) ~= 1 or
    tonumber(clean.OP_EnableWIP[comp.CurrentTime]) ~= 0 then
    fail("clean package switches are wrong")
end
if tonumber(wip.OP_ReviewWidth[comp.CurrentTime]) ~= 1920 or
    tonumber(wip.OP_ReviewHeight[comp.CurrentTime]) ~= 1080 then
    fail("Review Resolution was not applied")
end
if tostring(wip.OP_TLText[comp.CurrentTime]) ~= "WIP · {frame}" or
    tostring(wip.OP_BRText[comp.CurrentTime]) ~= "{timecode}" then
    fail("WIP text template is wrong")
end
if wip_saver:GetAttrs().TOOLB_PassThrough == true or
    clean_saver:GetAttrs().TOOLB_PassThrough == true then
    fail("example Savers are disabled")
end

print("OUTPUTPACKAGER_EXAMPLE_HOST_TEST_OK")
comp:SetActiveTool(config)
