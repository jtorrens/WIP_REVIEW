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

    comp.CurrentTime = 0
    comp:Unlock()

    local function render_probe(reg_id, name, placement, canvas_mode, x, y,
                                blanking_enabled, blanking_preset,
                                blanking_custom, blanking_opacity,
                                text_enabled, text_value, text_anchor,
                                font_family, font_style, font_size, text_opacity)
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
        probe.staticTextEnabled = text_enabled or 0
        probe.staticText = text_value or "SECUENCIA ÁRTICO — VERSIÓN 03"
        probe.staticTextAnchor = text_anchor or 0
        probe.fontFamily = font_family or "System Default"
        probe.fontStyle = font_style or 0
        probe.fontSize = font_size or 0.028
        probe.textOpacity = text_opacity or 1.0
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
            "AUTOMATED_P1A_FILTER_" .. placement_names[placement + 1],
            placement, 1, placement + 1, 0)
    end
    render_probe("ofx.com.jtorrens.WIPReviewProbe", "AUTOMATED_P1A_GENERAL_FIT", 1, 1, 1, 1)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P1A_HOST_RASTER", 0, 0, 2, 1)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P1B_B01_2_00", 0, 1, 1, 2,
                 1, 2, 2.0, 1.0)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P1B_B02_HALF", 0, 1, 2, 2,
                 1, 2, 2.0, 0.5)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P1B_B03_OFF", 0, 1, 3, 2,
                 0, 2, 2.0, 1.0)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P1B_B04_PILLAR", 0, 1, 4, 2,
                 1, 4, 1.33, 1.0)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P1C_UTF8_TL", 0, 1, 1, 3,
                 0, 2, 2.0, 1.0, 1, "SECUENCIA ÁRTICO — VERSIÓN 03", 0,
                 "System Default", 0, 0.028, 1.0)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P1C_TOP_LARGE", 0, 1, 2, 3,
                 0, 2, 2.0, 1.0, 1, "TOP GROWS DOWN", 0,
                 "System Default", 1, 0.056, 1.0)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P1C_BOTTOM_LARGE", 0, 1, 3, 3,
                 0, 2, 2.0, 1.0, 1, "BOTTOM GROWS UP", 3,
                 "System Default", 1, 0.056, 1.0)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P1C_FONT_FALLBACK", 0, 1, 4, 3,
                 0, 2, 2.0, 1.0, 1, "FONT FALLBACK", 2,
                 "WIPReview Font That Does Not Exist 7F3A", 0, 0.028, 1.0)
    render_probe("ofx.com.jtorrens.WIPReviewProbe.Filter", "AUTOMATED_P1C_OVER_BLANKING", 0, 1, 5, 3,
                 1, 2, 2.0, 1.0, 1, "TEXT OVER BLANKING", 1,
                 "System Default", 0, 0.028, 1.0)

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
