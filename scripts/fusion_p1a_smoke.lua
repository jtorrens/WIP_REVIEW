-- Automated Fusion Standalone host smoke test for WIP Review.
-- Creates and closes a private unsaved composition; it never modifies the
-- composition that was active when the script started.

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then
    error("Fusion Standalone is not reachable through FusionScript")
end

local previous = fusion.CurrentComp
local comp = fusion:NewComp()
if comp == nil then
    error("Fusion failed to create an isolated test composition")
end

local function require_tool(reg_id, x, y)
    local tool = comp:AddTool(reg_id, x, y)
    if tool == nil then
        error("Unable to add tool: " .. reg_id)
    end
    return tool
end

local ok, failure = pcall(function()
    comp:Lock()
    comp:SetAttrs({
        COMPS_Name = "WIPReview_Automated_P1A_Smoke",
        COMPN_GlobalStart = 0,
        COMPN_GlobalEnd = 1,
        COMPN_RenderStart = 0,
        COMPN_RenderEnd = 1,
    })

    local source = require_tool("Background", 0, 0)
    source:SetAttrs({TOOLS_Name = "AutomatedSource4608x3164"})
    source.UseFrameFormatSettings = 0
    source.Width = 4608
    source.Height = 3164
    source.PixelAspect = {1, 1}
    source.TopLeftRed = 0.125
    source.TopLeftGreen = 0.25
    source.TopLeftBlue = 0.5
    source.TopLeftAlpha = 1.0

    local probe = require_tool("ofx.com.jtorrens.WIPReviewProbe.Filter", 1, 0)
    probe:SetAttrs({TOOLS_Name = "AutomatedWIPReviewFilter"})
    probe.Source = source.Output
    probe.requestCustomRoD = 1
    probe.requestedWidth = 1920
    probe.requestedHeight = 1080
    probe.canvasMode = 1
    probe.placementMode = 1
    probe.resampleFilter = 2
    probe.scenarioLabel = "AUTOMATED_FUSION_P1A_SMOKE"
    probe.AllowResize = 1

    comp:SetActiveTool(probe)
    comp.CurrentTime = 0
    comp:Unlock()

    local image = probe.Output:GetValue(0)
    if image == nil then
        error("Fusion returned no image from the automated OFX graph")
    end

    print("WIPREVIEW_AUTOMATION_OK")
    print("fusion_version=" .. tostring(fusion:GetAttrs().FUSIONS_Version))
    print("source_tool=" .. tostring(source:GetAttrs().TOOLS_RegID))
    print("probe_tool=" .. tostring(probe:GetAttrs().TOOLS_RegID))
end)

if comp ~= nil then
    -- FusionScript intentionally suppresses the save prompt when Close() is
    -- called while the composition is locked.
    pcall(function() comp:Lock() end)
    pcall(function() comp:Close() end)
end
if previous ~= nil then
    pcall(function() fusion:SetActiveComp(previous) end)
end

if not ok then
    error(failure)
end
