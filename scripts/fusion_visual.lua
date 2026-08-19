-- Opens an unsaved visual-validation composition and intentionally leaves it
-- active for human inspection. It never modifies the previously active comp.
local fusion = nil
for attempt = 1, 60 do
    fusion = bmd.scriptapp("Fusion", "localhost")
    if fusion ~= nil then break end
    bmd.wait(1)
end
if fusion == nil then
    error("Fusion Standalone is not reachable")
end

local chart_path = os.getenv("WIPREVIEW_VISUAL_CHART") or
                   "/private/tmp/wipreview-visual-chart.png"
local previous = fusion.CurrentComp
local comp = nil
for attempt = 1, 30 do
    comp = fusion:NewComp()
    if comp ~= nil then break end
    bmd.wait(1)
end
if comp == nil then
    error("Fusion failed to create the visual-validation composition")
end

local ok, failure = pcall(function()
    comp:Lock()
    comp:SetAttrs({
        COMPS_Name = "WIPReview_VISUAL_VALIDATION_DO_NOT_SAVE",
        COMPN_GlobalStart = 0,
        COMPN_GlobalEnd = 0,
        COMPN_RenderStart = 0,
        COMPN_RenderEnd = 0,
    })

    local loader = comp:AddTool("Loader", 0, 0)
    if loader == nil then error("Unable to create chart Loader") end
    loader:SetAttrs({TOOLS_Name = "SOURCE_CHART_4608x3164"})
    loader.Clip = chart_path

    local names = {"IDENTITY", "FIT", "FILL_CROP", "STRETCH", "ONE_TO_ONE"}
    local probes = {}
    for placement = 0, 4 do
        local probe = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe.Filter", placement + 1, 0)
        if probe == nil then error("Unable to create geometry probe " .. names[placement + 1]) end
        probe:SetAttrs({TOOLS_Name = "GEOMETRY_" .. names[placement + 1]})
        probe.Source = loader.Output
        probe.requestCustomRoD = 1
        probe.canvasMode = 1
        probe.requestedWidth = 1920
        probe.requestedHeight = 1080
        probe.placementMode = placement
        probe.resampleFilter = 2
        probe.scenarioLabel = "VISUAL_GEOMETRY_" .. names[placement + 1]
        probe.AllowResize = 1
        probes[placement + 1] = probe
    end

    local host = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe.Filter", 3, 1)
    if host == nil then error("Unable to create Host Raster probe") end
    host:SetAttrs({TOOLS_Name = "GEOMETRY_HOST_RASTER_IDENTITY"})
    host.Source = loader.Output
    host.requestCustomRoD = 1
    host.canvasMode = 0
    host.placementMode = 0
    host.resampleFilter = 2
    host.scenarioLabel = "VISUAL_GEOMETRY_HOST_RASTER"
    host.AllowResize = 1

    local function add_blanking(name, preset, custom_aspect, opacity, enabled, x)
        local probe = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe.Filter", x, 2)
        if probe == nil then error("Unable to create blanking probe " .. name) end
        probe:SetAttrs({TOOLS_Name = name})
        probe.Source = loader.Output
        probe.requestCustomRoD = 1
        probe.canvasMode = 1
        probe.requestedWidth = 1920
        probe.requestedHeight = 1080
        -- Fill/Crop ensures the picture occupies the entire canvas so the
        -- visual check isolates blanking from Fit's own pillar/letterbox.
        probe.placementMode = 2
        probe.resampleFilter = 2
        probe.blankingEnabled = enabled
        probe.blankingAspectPreset = preset
        probe.blankingAspectCustom = custom_aspect
        probe.blankingOpacity = opacity
        probe.scenarioLabel = "VISUAL_" .. name
        probe.AllowResize = 1
        return probe
    end

    add_blanking("BLANKING_2_00_OPAQUE", 2, 2.0, 1.0, 1, 1)
    add_blanking("BLANKING_2_00_HALF", 2, 2.0, 0.5, 1, 2)
    add_blanking("BLANKING_OFF", 2, 2.0, 1.0, 0, 3)
    add_blanking("BLANKING_PILLAR_1_33", 4, 1.33, 1.0, 1, 4)

    local function add_text(name, text, anchor, font_size, font_style,
                            font_family, blanking_enabled, x)
        local probe = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe.Filter", x, 4)
        if probe == nil then error("Unable to create static-text probe " .. name) end
        probe:SetAttrs({TOOLS_Name = name})
        probe.Source = loader.Output
        probe.requestCustomRoD = 1
        probe.canvasMode = 1
        probe.requestedWidth = 1920
        probe.requestedHeight = 1080
        probe.placementMode = 2
        probe.resampleFilter = 2
        probe.blankingEnabled = blanking_enabled
        probe.blankingAspectPreset = 2
        probe.blankingAspectCustom = 2.0
        probe.blankingOpacity = 1.0
        probe.fontFamily = font_family
        probe.fontStyle = font_style
        probe.fontSize = font_size
        probe.textOpacity = 1.0
        probe.outlineEnabled = 0
        probe.shadowEnabled = 0
        probe.zoneGap = 0.010
        probe.overflowMode = 2
        probe.minimumFontScale = 0.60
        probe.paddingLeft = 0.015
        probe.paddingRight = 0.015
        probe.paddingTop = 0.020
        probe.paddingBottom = 0.020
        if anchor == 0 then probe.tlEnabled = 1; probe.tlText = text
        elseif anchor == 1 then probe.tcEnabled = 1; probe.tcText = text
        elseif anchor == 2 then probe.trEnabled = 1; probe.trText = text
        elseif anchor == 3 then probe.blEnabled = 1; probe.blText = text
        elseif anchor == 4 then probe.bcEnabled = 1; probe.bcText = text
        else probe.brEnabled = 1; probe.brText = text end
        probe.scenarioLabel = "VISUAL_" .. name
        probe.AllowResize = 1
        return probe
    end

    add_text(
        "P2A_TEXT_UTF8_TOP_LEFT", "SECUENCIA ÁRTICO — VERSIÓN 03", 0,
        0.040, 0, "System Default", 0, 1)
    add_text("P2A_TEXT_UTF8_BOTTOM_RIGHT", "SECUENCIA ÁRTICO — VERSIÓN 03", 5,
             0.040, 0, "System Default", 0, 2)
    add_text("P2A_TEXT_TOP_LARGE", "TOP GROWS DOWN", 0,
             0.080, 1, "System Default", 0, 3)
    add_text("P2A_TEXT_BOTTOM_LARGE", "BOTTOM GROWS UP", 3,
             0.080, 1, "System Default", 0, 4)
    add_text("P2A_TEXT_OVER_BLANKING", "TEXT OVER BLANKING", 1,
             0.040, 0, "System Default", 1, 5)
    add_text("P2A_TEXT_FONT_FALLBACK", "FONT FALLBACK", 2,
             0.040, 0, "WIPReview Font That Does Not Exist 7F3A", 0, 6)

    local function add_zones(name, blanking_enabled, configure, x)
        local probe = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe.Filter", x, 6)
        if probe == nil then error("Unable to create six-zone probe " .. name) end
        probe:SetAttrs({TOOLS_Name = name})
        probe.Source = loader.Output
        probe.requestCustomRoD = 1
        probe.canvasMode = 1
        probe.requestedWidth = 1920
        probe.requestedHeight = 1080
        probe.placementMode = 2
        probe.resampleFilter = 2
        probe.blankingEnabled = blanking_enabled
        probe.blankingAspectPreset = 2
        probe.blankingAspectCustom = 2.0
        probe.blankingOpacity = 1.0
        probe.fontFamily = "System Default"
        probe.fontStyle = 0
        probe.fontSize = 0.040
        probe.textOpacity = 1.0
        probe.outlineEnabled = 0
        probe.shadowEnabled = 0
        probe.zoneGap = 0.010
        probe.overflowMode = 2
        probe.minimumFontScale = 0.60
        probe.paddingLeft = 0.015
        probe.paddingRight = 0.015
        probe.paddingTop = 0.020
        probe.paddingBottom = 0.020
        configure(probe)
        probe.scenarioLabel = "VISUAL_" .. name
        probe.AllowResize = 1
        return probe
    end

    add_zones("P2A_SIX_ZONES", 0, function(p)
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlText = "TOP LEFT Á"; p.tcText = "TOP CENTER —"; p.trText = "TOP RIGHT Ó"
        p.blText = "BOTTOM LEFT"; p.bcText = "BOTTOM CENTER"; p.brText = "BOTTOM RIGHT"
    end, 1)
    add_zones("P2A_OVERRIDES", 0, function(p)
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlUseSizeOverride = 1; p.tlSize = 0.080
        p.tcUseColorOverride = 1
        p.tcColorRed = 0.0; p.tcColorGreen = 1.0
        p.tcColorBlue = 0.0; p.tcColorAlpha = 1.0
        p.trUseOpacityOverride = 1; p.trOpacity = 0.25
        p.blOffsetX = 0.05; p.blOffsetY = 0.04
        p.tlText = "TL LARGE"; p.tcText = "TC GREEN"; p.trText = "TR 25%"
        p.blText = "BL OFFSET"; p.bcText = "BC GLOBAL"; p.brText = "BR GLOBAL"
    end, 2)
    add_zones("P2A_OFFSETS_INWARD", 0, function(p)
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlOffsetX = 0.05; p.tlOffsetY = -0.08
        p.tcOffsetY = -0.08
        p.trOffsetX = -0.05; p.trOffsetY = -0.08
        p.blOffsetX = 0.05; p.blOffsetY = 0.08
        p.bcOffsetY = 0.08
        p.brOffsetX = -0.05; p.brOffsetY = 0.08
        p.tlText = "TL IN"; p.tcText = "TC IN"; p.trText = "TR IN"
        p.blText = "BL IN"; p.bcText = "BC IN"; p.brText = "BR IN"
    end, 3)
    add_zones("P2A_WITH_BLANKING", 1, function(p)
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlText = "TL ON BAR"; p.tcText = "TC ON BAR"; p.trText = "TR ON BAR"
        p.blText = "BL ON BAR"; p.bcText = "BC ON BAR"; p.brText = "BR ON BAR"
    end, 4)

    add_zones("P2B_OUTLINE_DEFAULT", 0, function(p)
        p.outlineEnabled = 1; p.outlineWidth = 0.001
        p.tcEnabled = 1; p.tcText = "DEFAULT BLACK OUTLINE"
    end, 5)
    add_zones("P2B_OUTLINE_WIDE_RED_HALF", 0, function(p)
        p.outlineEnabled = 1; p.outlineWidth = 0.006
        p.outlineColorRed = 1.0; p.outlineColorGreen = 0.0
        p.outlineColorBlue = 0.0; p.outlineColorAlpha = 1.0
        p.outlineOpacity = 0.5
        p.bcEnabled = 1; p.bcText = "WIDE RED 50% OUTLINE"
    end, 6)
    add_zones("P2B_OUTLINE_SIX_ZONES", 1, function(p)
        p.outlineEnabled = 1; p.outlineWidth = 0.003
        p.outlineColorRed = 1.0; p.outlineColorGreen = 0.0
        p.outlineColorBlue = 0.0; p.outlineColorAlpha = 1.0
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlText = "TL OUT"; p.tcText = "TC OUT"; p.trText = "TR OUT"
        p.blText = "BL OUT"; p.bcText = "BC OUT"; p.brText = "BR OUT"
    end, 7)

    add_zones("P2C_SHADOW_DEFAULT", 0, function(p)
        p.fontSize = 0.080
        p.shadowEnabled = 1
        p.tcEnabled = 1; p.tcText = "DEFAULT BLACK SOFT SHADOW"
    end, 8)
    add_zones("P2C_SHADOW_HARD_BLUE", 0, function(p)
        p.fontSize = 0.080
        p.shadowEnabled = 1
        p.shadowOffsetX = 0.012; p.shadowOffsetY = 0.018
        p.shadowSoftness = 0.0
        p.shadowColorRed = 0.0; p.shadowColorGreen = 0.0
        p.shadowColorBlue = 1.0; p.shadowColorAlpha = 1.0
        p.shadowOpacity = 1.0
        p.bcEnabled = 1; p.bcText = "HARD BLUE SHADOW"
    end, 9)
    add_zones("P2C_SHADOW_SIX_ZONES", 1, function(p)
        p.shadowEnabled = 1
        p.shadowOffsetX = 0.006; p.shadowOffsetY = 0.009
        p.shadowSoftness = 0.004
        p.shadowColorRed = 0.0; p.shadowColorGreen = 1.0
        p.shadowColorBlue = 1.0; p.shadowColorAlpha = 1.0
        p.shadowOpacity = 0.8
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlText = "TL SHADOW"; p.tcText = "TC SHADOW"; p.trText = "TR SHADOW"
        p.blText = "BL SHADOW"; p.bcText = "BC SHADOW"; p.brText = "BR SHADOW"
    end, 10)

    local overflow_clip = add_zones("P2D_OVERFLOW_CLIP", 0, function(p)
        p.overflowMode = 0; p.fontSize = 0.080
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.tlUseColorOverride = 1
        p.tlColorRed = 1.0; p.tlColorGreen = 0.0; p.tlColorBlue = 0.0
        p.tcUseColorOverride = 1
        p.tcColorRed = 0.0; p.tcColorGreen = 1.0; p.tcColorBlue = 0.0
        p.trUseColorOverride = 1
        p.trColorRed = 0.0; p.trColorGreen = 0.4; p.trColorBlue = 1.0
        p.tlText = "LEFT CLIPS AT ITS CELL BOUNDARY"
        p.tcText = "CENTER CLIPS AT BOTH CELL BOUNDARIES"
        p.trText = "RIGHT CLIPS AT ITS CELL BOUNDARY"
    end, 11)
    add_zones("P2D_OVERFLOW_ELLIPSIS", 0, function(p)
        p.overflowMode = 1; p.fontSize = 0.060
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlText = "LEFT ELLIPSIS PRESERVES ÁRTICO"
        p.tcText = "CENTER ELLIPSIS PRESERVES VERSIÓN"
        p.trText = "RIGHT ELLIPSIS PRESERVES SECUENCIA"
        p.blText = "BOTTOM LEFT ELLIPSIS"
        p.bcText = "BOTTOM CENTER ELLIPSIS"
        p.brText = "BOTTOM RIGHT ELLIPSIS"
    end, 12)
    add_zones("P2D_OVERFLOW_SHRINK", 0, function(p)
        p.overflowMode = 2; p.fontSize = 0.080; p.minimumFontScale = 0.60
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlText = "LEFT SHRINKS TO FIT"
        p.tcText = "CENTER SHRINKS TO FIT"
        p.trText = "RIGHT SHRINKS TO FIT"
        p.blText = "SHORT"; p.bcText = "SHORT"; p.brText = "SHORT"
    end, 13)
    add_zones("P2D_OVERFLOW_MIN_CLIP", 1, function(p)
        p.overflowMode = 2; p.fontSize = 0.080; p.minimumFontScale = 0.60
        p.tcEnabled = 1
        p.tcText = "MINIMUM SCALE CANNOT FIT THIS EXTREMELY LONG STRING SO THE CELL CLIPS IT"
    end, 14)

    comp.CurrentTime = 0
    comp:SetActiveTool(overflow_clip)
    comp:Unlock()
    print("WIPREVIEW_VISUAL_READY")
    print("active_tool=P2D_OVERFLOW_CLIP")
    print("chart=" .. chart_path)
end)

if not ok then
    pcall(function() comp:Lock() end)
    pcall(function() comp:Close() end)
    if previous ~= nil then pcall(function() fusion:SetActiveComp(previous) end) end
    error(failure)
end
