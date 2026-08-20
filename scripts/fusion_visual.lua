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
        COMPN_GlobalEnd = 1800,
        COMPN_RenderStart = 0,
        COMPN_RenderEnd = 1800,
    })

    local loader = comp:AddTool("Loader", 0, 0)
    if loader == nil then error("Unable to create chart Loader") end
    loader:SetAttrs({TOOLS_Name = "SOURCE_CHART_4608x3164"})
    loader.Clip = chart_path

    local names = {"IDENTITY", "FIT", "FILL_CROP", "STRETCH", "ONE_TO_ONE"}
    local probes = {}
    for placement = 0, 4 do
        local probe = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe", placement + 1, 0)
        if probe == nil then error("Unable to create geometry probe " .. names[placement + 1]) end
        probe:SetAttrs({TOOLS_Name = "GEOMETRY_" .. names[placement + 1]})
        probe.Source = loader.Output
        probe.canvasMode = 1
        probe.reviewRasterPreset = 0
        probe.requestedWidth = 1920
        probe.requestedHeight = 1080
        probe.placementMode = placement
        probe.resampleFilter = 2
        probe.colorSpaceMode = 1
        probe.manualColorSpace = 0
        probe.AllowResize = 1
        probes[placement + 1] = probe
    end

    local host = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe", 3, 1)
    if host == nil then error("Unable to create Host Raster probe") end
    host:SetAttrs({TOOLS_Name = "GEOMETRY_HOST_RASTER_IDENTITY"})
    host.Source = loader.Output
    host.canvasMode = 0
    host.placementMode = 0
    host.resampleFilter = 2
    host.colorSpaceMode = 1
    host.manualColorSpace = 0
    host.AllowResize = 1

    local function add_blanking(name, preset, custom_aspect, opacity, enabled, x)
        local probe = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe", x, 2)
        if probe == nil then error("Unable to create blanking probe " .. name) end
        probe:SetAttrs({TOOLS_Name = name})
        probe.Source = loader.Output
        probe.canvasMode = 1
        probe.reviewRasterPreset = 0
        probe.requestedWidth = 1920
        probe.requestedHeight = 1080
        -- Fill/Crop ensures the picture occupies the entire canvas so the
        -- visual check isolates blanking from Fit's own pillar/letterbox.
        probe.placementMode = 2
        probe.resampleFilter = 2
        probe.colorSpaceMode = 1
        probe.manualColorSpace = 0
        probe.blankingEnabled = enabled
        probe.blankingAspectPreset = preset
        probe.blankingAspectCustom = custom_aspect
        probe.blankingOpacity = opacity
        probe.AllowResize = 1
        return probe
    end

    add_blanking("BLANKING_2_00_OPAQUE", 2, 2.0, 1.0, 1, 1)
    add_blanking("BLANKING_2_00_HALF", 2, 2.0, 0.5, 1, 2)
    add_blanking("BLANKING_OFF", 2, 2.0, 1.0, 0, 3)
    add_blanking("BLANKING_PILLAR_1_33", 4, 1.33, 1.0, 1, 4)

    local function add_text(name, text, anchor, font_size, font_style,
                            font_family, blanking_enabled, x)
        local probe = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe", x, 4)
        if probe == nil then error("Unable to create static-text probe " .. name) end
        probe:SetAttrs({TOOLS_Name = name})
        probe.Source = loader.Output
        probe.canvasMode = 1
        probe.reviewRasterPreset = 0
        probe.requestedWidth = 1920
        probe.requestedHeight = 1080
        probe.placementMode = 2
        probe.resampleFilter = 2
        probe.colorSpaceMode = 1
        probe.manualColorSpace = 0
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
        probe.frameRelativeBase = 1
        probe.frameStart = 1001
        probe.fpsMode = 1
        probe.fpsOverride = 24.0
        probe.timecodeStart = "00:00:00:00"
        probe.dropFrameMode = 1
        probe.colorSpaceMode = 1
        probe.manualColorSpace = 0
        probe.graphicsWhiteMode = 0
        probe.graphicsWhiteNits = 203.0
        probe.hlgPeakNits = 1000.0
        probe.paddingLeft = 0.015
        probe.paddingRight = 0.015
        probe.paddingTop = 0.020
        probe.paddingBottom = 0.020
        if anchor == 0 then probe.tlEnabled = 1; probe.tlPrefix = text
        elseif anchor == 1 then probe.tcEnabled = 1; probe.tcPrefix = text
        elseif anchor == 2 then probe.trEnabled = 1; probe.trPrefix = text
        elseif anchor == 3 then probe.blEnabled = 1; probe.blPrefix = text
        elseif anchor == 4 then probe.bcEnabled = 1; probe.bcPrefix = text
        else probe.brEnabled = 1; probe.brPrefix = text end
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
        local probe = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe", x, 6)
        if probe == nil then error("Unable to create six-zone probe " .. name) end
        probe:SetAttrs({TOOLS_Name = name})
        probe.Source = loader.Output
        probe.canvasMode = 1
        probe.reviewRasterPreset = 0
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
        probe.frameRelativeBase = 1
        probe.frameStart = 1001
        probe.fpsMode = 1
        probe.fpsOverride = 24.0
        probe.timecodeStart = "00:00:00:00"
        probe.dropFrameMode = 1
        probe.paddingLeft = 0.015
        probe.paddingRight = 0.015
        probe.paddingTop = 0.020
        probe.paddingBottom = 0.020
        configure(probe)
        probe.AllowResize = 1
        return probe
    end

    add_zones("P2A_SIX_ZONES", 0, function(p)
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlPrefix = "TOP LEFT Á"; p.tcPrefix = "TOP CENTER —"; p.trPrefix = "TOP RIGHT Ó"
        p.blPrefix = "BOTTOM LEFT"; p.bcPrefix = "BOTTOM CENTER"; p.brPrefix = "BOTTOM RIGHT"
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
        p.tlPrefix = "TL LARGE"; p.tcPrefix = "TC GREEN"; p.trPrefix = "TR 25%"
        p.blPrefix = "BL OFFSET"; p.bcPrefix = "BC GLOBAL"; p.brPrefix = "BR GLOBAL"
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
        p.tlPrefix = "TL IN"; p.tcPrefix = "TC IN"; p.trPrefix = "TR IN"
        p.blPrefix = "BL IN"; p.bcPrefix = "BC IN"; p.brPrefix = "BR IN"
    end, 3)
    add_zones("P2A_WITH_BLANKING", 1, function(p)
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlPrefix = "TL ON BAR"; p.tcPrefix = "TC ON BAR"; p.trPrefix = "TR ON BAR"
        p.blPrefix = "BL ON BAR"; p.bcPrefix = "BC ON BAR"; p.brPrefix = "BR ON BAR"
    end, 4)

    add_zones("P2B_OUTLINE_DEFAULT", 0, function(p)
        p.outlineEnabled = 1; p.outlineWidth = 0.001
        p.tcEnabled = 1; p.tcPrefix = "DEFAULT BLACK OUTLINE"
    end, 5)
    add_zones("P2B_OUTLINE_WIDE_RED_HALF", 0, function(p)
        p.outlineEnabled = 1; p.outlineWidth = 0.006
        p.outlineColorRed = 1.0; p.outlineColorGreen = 0.0
        p.outlineColorBlue = 0.0; p.outlineColorAlpha = 1.0
        p.outlineOpacity = 0.5
        p.bcEnabled = 1; p.bcPrefix = "WIDE RED 50% OUTLINE"
    end, 6)
    add_zones("P2B_OUTLINE_SIX_ZONES", 1, function(p)
        p.outlineEnabled = 1; p.outlineWidth = 0.003
        p.outlineColorRed = 1.0; p.outlineColorGreen = 0.0
        p.outlineColorBlue = 0.0; p.outlineColorAlpha = 1.0
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlPrefix = "TL OUT"; p.tcPrefix = "TC OUT"; p.trPrefix = "TR OUT"
        p.blPrefix = "BL OUT"; p.bcPrefix = "BC OUT"; p.brPrefix = "BR OUT"
    end, 7)

    add_zones("P2C_SHADOW_DEFAULT", 0, function(p)
        p.fontSize = 0.080
        p.shadowEnabled = 1
        p.tcEnabled = 1; p.tcPrefix = "DEFAULT BLACK SOFT SHADOW"
    end, 8)
    add_zones("P2C_SHADOW_HARD_BLUE", 0, function(p)
        p.fontSize = 0.080
        p.shadowEnabled = 1
        p.shadowOffsetX = 0.012; p.shadowOffsetY = 0.018
        p.shadowSoftness = 0.0
        p.shadowColorRed = 0.0; p.shadowColorGreen = 0.0
        p.shadowColorBlue = 1.0; p.shadowColorAlpha = 1.0
        p.shadowOpacity = 1.0
        p.bcEnabled = 1; p.bcPrefix = "HARD BLUE SHADOW"
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
        p.tlPrefix = "TL SHADOW"; p.tcPrefix = "TC SHADOW"; p.trPrefix = "TR SHADOW"
        p.blPrefix = "BL SHADOW"; p.bcPrefix = "BC SHADOW"; p.brPrefix = "BR SHADOW"
    end, 10)

    add_zones("P2D_OVERFLOW_CLIP", 0, function(p)
        p.overflowMode = 0; p.fontSize = 0.080
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.tlUseColorOverride = 1
        p.tlColorRed = 1.0; p.tlColorGreen = 0.0; p.tlColorBlue = 0.0
        p.tcUseColorOverride = 1
        p.tcColorRed = 0.0; p.tcColorGreen = 1.0; p.tcColorBlue = 0.0
        p.trUseColorOverride = 1
        p.trColorRed = 0.0; p.trColorGreen = 0.4; p.trColorBlue = 1.0
        p.tlPrefix = "LEFT CLIPS AT ITS CELL BOUNDARY"
        p.tcPrefix = "CENTER CLIPS AT BOTH CELL BOUNDARIES"
        p.trPrefix = "RIGHT CLIPS AT ITS CELL BOUNDARY"
    end, 11)
    add_zones("P2D_OVERFLOW_ELLIPSIS", 0, function(p)
        p.overflowMode = 1; p.fontSize = 0.060
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlPrefix = "LEFT ELLIPSIS PRESERVES ÁRTICO"
        p.tcPrefix = "CENTER ELLIPSIS PRESERVES VERSIÓN"
        p.trPrefix = "RIGHT ELLIPSIS PRESERVES SECUENCIA"
        p.blPrefix = "BOTTOM LEFT ELLIPSIS"
        p.bcPrefix = "BOTTOM CENTER ELLIPSIS"
        p.brPrefix = "BOTTOM RIGHT ELLIPSIS"
    end, 12)
    add_zones("P2D_OVERFLOW_SHRINK", 0, function(p)
        p.overflowMode = 2; p.fontSize = 0.080; p.minimumFontScale = 0.60
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.blEnabled = 1; p.bcEnabled = 1; p.brEnabled = 1
        p.tlPrefix = "LEFT SHRINKS TO FIT"
        p.tcPrefix = "CENTER SHRINKS TO FIT"
        p.trPrefix = "RIGHT SHRINKS TO FIT"
        p.blPrefix = "SHORT"; p.bcPrefix = "SHORT"; p.brPrefix = "SHORT"
    end, 13)
    add_zones("P2D_OVERFLOW_MIN_CLIP", 1, function(p)
        p.overflowMode = 2; p.fontSize = 0.080; p.minimumFontScale = 0.60
        p.tcEnabled = 1
        p.tcPrefix = "MINIMUM SCALE CANNOT FIT THIS EXTREMELY LONG STRING SO THE CELL CLIPS IT"
    end, 14)

    local calculated_fields = add_zones("P3_CALCULATED_FIELDS_24_NDF", 0, function(p)
        p.frameRelativeBase = 1; p.frameStart = 1001
        p.fpsMode = 1; p.fpsOverride = 24.0
        p.timecodeStart = "00:00:00:00"; p.dropFrameMode = 1
        p.tlEnabled = 1; p.tcEnabled = 1; p.trEnabled = 1
        p.tlPrefix = "REL "; p.tlCalculatedField = 1
        p.tcPrefix = "ABS "; p.tcCalculatedField = 2
        p.trPrefix = "TC "; p.trCalculatedField = 3
        p.bcEnabled = 1; p.bcPrefix = "UNKNOWN REMAINS {shot}"
    end, 15)
    add_zones("P3_TIMECODE_DF", 0, function(p)
        p.fpsMode = 1; p.fpsOverride = 30000.0 / 1001.0
        p.timecodeStart = "00:00:00;00"; p.dropFrameMode = 2
        p.tcEnabled = 1; p.tcPrefix = "29.97 DF "; p.tcCalculatedField = 3
    end, 16)
    add_zones("P3_INVALID_TIMECODE", 1, function(p)
        p.fpsMode = 1; p.fpsOverride = 25.0
        p.timecodeStart = "invalid"; p.dropFrameMode = 2
        p.tcEnabled = 1; p.tcPrefix = "INVALID TC "; p.tcCalculatedField = 3
    end, 17)

    local managed_color = add_zones("P4_REC709_DISPLAY_LINEAR_COMPOSITE", 1, function(p)
        p.colorSpaceMode = 1; p.manualColorSpace = 0
        p.graphicsWhiteMode = 0; p.blankingOpacity = 0.5
        p.tcEnabled = 1; p.tcPrefix = "REC709 OUTPUT / DISPLAY-LINEAR COMPOSITE"
    end, 18)
    add_zones("P4_PQ_GRAPHICS_WHITE_203", 0, function(p)
        p.colorSpaceMode = 1; p.manualColorSpace = 1
        p.graphicsWhiteMode = 0
        p.tcEnabled = 1; p.tcPrefix = "PQ GRAPHICS WHITE 203 NITS"
    end, 19)
    add_zones("P4_HLG_GRAPHICS_WHITE_203", 0, function(p)
        p.colorSpaceMode = 1; p.manualColorSpace = 2
        p.graphicsWhiteMode = 0; p.hlgPeakNits = 1000.0
        p.tcEnabled = 1; p.tcPrefix = "HLG GRAPHICS WHITE 203 NITS"
    end, 20)
    add_zones("P4_AUTO_UNKNOWN_WARNING", 0, function(p)
        p.colorSpaceMode = 0; p.manualColorSpace = 0
        p.tcEnabled = 1; p.tcPrefix = "AUTO UNKNOWN USES MANUAL REC709"
    end, 21)

    comp.CurrentTime = 0
    comp:SetActiveTool(managed_color)
    comp:Unlock()
    print("WIPREVIEW_VISUAL_READY")
    print("active_tool=P4_REC709_DISPLAY_LINEAR_COMPOSITE")
    print("chart=" .. chart_path)
end)

if not ok then
    pcall(function() comp:Lock() end)
    pcall(function() comp:Close() end)
    if previous ~= nil then pcall(function() fusion:SetActiveComp(previous) end) end
    error(failure)
end
