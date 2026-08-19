-- Automated Fusion Standalone host smoke test for WIP Review.
-- Creates and closes a private unsaved composition; it never modifies the
-- composition that was active when the script started.

local fusion = nil
for attempt = 1, 60 do
    fusion = bmd.scriptapp("Fusion", "localhost")
    if fusion ~= nil then
        break
    end
    bmd.wait(1)
end
if fusion == nil then
    error("Fusion Standalone is not reachable through FusionScript")
end

local previous = fusion.CurrentComp
local comp = nil
for attempt = 1, 30 do
    comp = fusion:NewComp()
    if comp ~= nil then
        break
    end
    bmd.wait(1)
end
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
        COMPS_Name = "WIPReview_Automated_Cumulative_Smoke",
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

    comp.CurrentTime = 0
    comp:Unlock()

    local function render_probe(reg_id, name, placement, canvas_mode, x, y,
                                blanking_enabled, blanking_preset,
                                blanking_custom, blanking_opacity, configure_zones)
        comp:Lock()
        local probe = require_tool(reg_id, x, y)
        probe:SetAttrs({TOOLS_Name = name})
        probe.Source = source.Output
        probe.requestCustomRoD = 1
        probe.requestedWidth = 1920
        probe.requestedHeight = 1080
        probe.canvasMode = canvas_mode
        probe.placementMode = placement
        probe.resampleFilter = 2
        probe.AllowResize = 1
        probe.blankingEnabled = blanking_enabled or 0
        probe.blankingAspectPreset = blanking_preset or 3
        probe.blankingAspectCustom = blanking_custom or 2.0
        probe.blankingOpacity = blanking_opacity or 1.0
        probe.fontFamily = "System Default"
        probe.fontStyle = 0
        probe.fontSize = 0.028
        probe.textOpacity = 1.0
        probe.outlineEnabled = 0
        if configure_zones ~= nil then configure_zones(probe) end
        -- Fusion may reset string values while subsequent geometry parameters
        -- invalidate the OFX node, so the diagnostic marker is assigned last.
        probe.scenarioLabel = name
        comp:SetActiveTool(probe)
        comp:Unlock()
        local image = probe.Output:GetValue(0)
        if image == nil then
            error("Fusion returned no image for " .. name)
        end
        print("rendered=" .. name)
        return probe
    end

    local placement_names = {"IDENTITY", "FIT", "FILL", "STRETCH", "ONE_TO_ONE"}
    local probe = nil
    for placement = 0, 4 do
        probe = render_probe(
            "ofx.com.jtorrens.WIPReviewProbe.Filter",
            "AUTOMATED_GEOMETRY_FILTER_" .. placement_names[placement + 1],
            placement, 1, placement + 1, 0)
    end
    render_probe("ofx.com.jtorrens.WIPReviewProbe", "AUTOMATED_GEOMETRY_GENERAL_FIT", 1, 1, 1, 1)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_GEOMETRY_HOST_RASTER", 0, 0, 2, 1)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_BLANKING_B01_2_00", 0, 1, 1, 2,
                 1, 2, 2.0, 1.0)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_BLANKING_B02_HALF", 0, 1, 2, 2,
                 1, 2, 2.0, 0.5)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_BLANKING_B03_OFF", 0, 1, 3, 2,
                 0, 2, 2.0, 1.0)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_BLANKING_B04_PILLAR", 0, 1, 4, 2,
                 1, 4, 1.33, 1.0)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P2A_TEXT_UTF8_TL", 0, 1, 1, 3,
                 0, 2, 2.0, 1.0, function(p)
        p.tlEnabled = 1; p.tlText = "SECUENCIA ÁRTICO — VERSIÓN 03"
    end)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P2A_TEXT_TOP_LARGE", 0, 1, 2, 3,
                 0, 2, 2.0, 1.0, function(p)
        p.fontStyle = 1; p.fontSize = 0.056
        p.tlEnabled = 1; p.tlText = "TOP GROWS DOWN"
    end)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P2A_TEXT_BOTTOM_LARGE", 0, 1, 3, 3,
                 0, 2, 2.0, 1.0, function(p)
        p.fontStyle = 1; p.fontSize = 0.056
        p.blEnabled = 1; p.blText = "BOTTOM GROWS UP"
    end)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P2A_TEXT_FONT_FALLBACK", 0, 1, 4, 3,
                 0, 2, 2.0, 1.0, function(p)
        p.fontFamily = "WIPReview Font That Does Not Exist 7F3A"
        p.trEnabled = 1; p.trText = "FONT FALLBACK"
    end)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P2A_TEXT_OVER_BLANKING", 0, 1, 5, 3,
                 1, 2, 2.0, 1.0, function(p)
        p.tcEnabled = 1; p.tcText = "TEXT OVER BLANKING"
    end)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P2A_SIX_ZONES", 2, 1, 1, 4,
                 0, 2, 2.0, 1.0, function(p)
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlText = "TL Á"; p.tcText = "TC —"; p.trText = "TR Ó"
        p.blText = "BL"; p.bcText = "BC"; p.brText = "BR"
    end)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P2A_OVERRIDES", 2, 1, 2, 4,
                 0, 2, 2.0, 1.0, function(p)
        p.tlEnabled = 1; p.tcEnabled = 1; p.brEnabled = 1; p.blEnabled = 1
        p.tlUseSizeOverride = 1; p.tlSize = 0.056
        p.tcUseColorOverride = 1
        p.tcColorRed = 0.0; p.tcColorGreen = 1.0
        p.tcColorBlue = 0.0; p.tcColorAlpha = 1.0
        p.brUseOpacityOverride = 1; p.brOpacity = 0.25
        p.blOffsetX = 0.05; p.blOffsetY = 0.04
        p.tlText = "TL LARGE"; p.tcText = "TC GREEN"
        p.brText = "BR 25%"; p.blText = "BL OFFSET"
    end)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P2A_WITH_BLANKING", 2, 1, 3, 4,
                 1, 2, 2.0, 1.0, function(p)
        p.tlEnabled = 1; p.brEnabled = 1
        p.tlText = "TL OVER BLANKING"; p.brText = "BR OVER BLANKING"
    end)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P2B_OUTLINE_DEFAULT", 2, 1, 4, 4,
                 0, 2, 2.0, 1.0, function(p)
        p.outlineEnabled = 1; p.outlineWidth = 0.001
        p.tlEnabled = 1; p.tlText = "P2B DEFAULT OUTLINE"
    end)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P2B_OUTLINE_WIDE_RED_HALF", 2, 1, 5, 4,
                 0, 2, 2.0, 1.0, function(p)
        p.outlineEnabled = 1; p.outlineWidth = 0.006
        p.outlineColorRed = 1.0; p.outlineColorGreen = 0.0
        p.outlineColorBlue = 0.0; p.outlineColorAlpha = 1.0
        p.outlineOpacity = 0.5
        p.tcEnabled = 1; p.tcText = "P2B RED 50% OUTLINE"
    end)

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
