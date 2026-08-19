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
                   "/private/tmp/wipreview-p1a-chart.png"
local previous = fusion.CurrentComp
local comp = nil
for attempt = 1, 30 do
    comp = fusion:NewComp()
    if comp ~= nil then break end
    bmd.wait(1)
end
if comp == nil then
    error("Fusion failed to create the P1a visual-validation composition")
end

local ok, failure = pcall(function()
    comp:Lock()
    comp:SetAttrs({
        COMPS_Name = "WIPReview_P1A_VISUAL_VALIDATION_DO_NOT_SAVE",
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
        if probe == nil then error("Unable to create P1a probe " .. names[placement + 1]) end
        probe:SetAttrs({TOOLS_Name = "P1A_" .. names[placement + 1]})
        probe.Source = loader.Output
        probe.requestCustomRoD = 1
        probe.canvasMode = 1
        probe.requestedWidth = 1920
        probe.requestedHeight = 1080
        probe.placementMode = placement
        probe.resampleFilter = 2
        probe.scenarioLabel = "VISUAL_P1A_" .. names[placement + 1]
        probe.AllowResize = 1
        probes[placement + 1] = probe
    end

    local host = comp:AddTool("ofx.com.jtorrens.WIPReviewProbe.Filter", 3, 1)
    if host == nil then error("Unable to create Host Raster probe") end
    host:SetAttrs({TOOLS_Name = "P1A_HOST_RASTER_IDENTITY"})
    host.Source = loader.Output
    host.requestCustomRoD = 1
    host.canvasMode = 0
    host.placementMode = 0
    host.resampleFilter = 2
    host.scenarioLabel = "VISUAL_P1A_HOST_RASTER"
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
        probe.placementMode = 1
        probe.resampleFilter = 2
        probe.blankingEnabled = enabled
        probe.blankingAspectPreset = preset
        probe.blankingAspectCustom = custom_aspect
        probe.blankingOpacity = opacity
        probe.scenarioLabel = "VISUAL_" .. name
        probe.AllowResize = 1
        return probe
    end

    local blanking_opaque = add_blanking("P1B_BLANK_2_00_OPAQUE", 2, 2.0, 1.0, 1, 1)
    add_blanking("P1B_BLANK_2_00_HALF", 2, 2.0, 0.5, 1, 2)
    add_blanking("P1B_BLANK_OFF", 2, 2.0, 1.0, 0, 3)
    add_blanking("P1B_PILLAR_1_33", 4, 1.33, 1.0, 1, 4)

    comp.CurrentTime = 0
    comp:SetActiveTool(blanking_opaque)
    comp:Unlock()
    print("WIPREVIEW_VISUAL_READY")
    print("active_tool=P1B_BLANK_2_00_OPAQUE")
    print("chart=" .. chart_path)
end)

if not ok then
    pcall(function() comp:Lock() end)
    pcall(function() comp:Close() end)
    if previous ~= nil then pcall(function() fusion:SetActiveComp(previous) end) end
    error(failure)
end
