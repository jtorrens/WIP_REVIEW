-- Fusion 21 validation for the OutputPackager review-raster pipeline.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end

local temp_comp = dofile(script_directory() .. "../../test_support/temp_comp.lua")
local comp = temp_comp.acquire(fusion)
if comp == nil then error("Fusion could not create the review-raster test comp") end
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local function fail(message)
    error("OutputPackager review-raster failure: " .. message, 2)
end

local function add_tool(reg_id, name, x, y)
    local tool = comp:AddTool(reg_id, x, y)
    if tool == nil then fail("unable to add " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    return tool
end

local function dimensions(output)
    local image = output[comp.CurrentTime]
    if image == nil then fail("Fusion returned no image") end
    return tonumber(image.Width), tonumber(image.Height)
end

local function set_input_by_id(tool, id, value)
    for _, input in pairs(tool:GetInputList() or {}) do
        local attrs = input:GetAttrs() or {}
        if tostring(attrs.INPS_ID) == id then
            input[comp.CurrentTime] = value
            return
        end
    end
    fail((tool:GetAttrs().TOOLS_Name or "tool") .. " has no input " .. id)
end

local function assert_dimensions(output, expected_width, expected_height, label)
    local width, height = dimensions(output)
    if width ~= expected_width or height ~= expected_height then
        fail(string.format("%s expected %dx%d, got %sx%s",
            label, expected_width, expected_height, tostring(width), tostring(height)))
    end
end

comp:Lock()
local source = add_tool("Background", "ReviewRaster_Source_2_00", -4, 0)
source.Width[0] = 400
source.Height[0] = 200
source.TopLeftRed[0] = 0.2
source.TopLeftGreen[0] = 0.4
source.TopLeftBlue[0] = 0.8
source.TopLeftAlpha[0] = 1

local fit = add_tool("BetterResize", "ReviewRaster_CenteredFit", -2, 0)
fit.Width[0] = 192
fit.Height[0] = 108
fit.KeepAspect[0] = 1
fit.FilterMethod[0] = 3
fit.Input = source.Output

local canvas = add_tool("Background", "ReviewRaster_OpaqueBlackCanvas", -2, 1)
canvas.Width[0] = 192
canvas.Height[0] = 108
canvas.TopLeftRed[0] = 0
canvas.TopLeftGreen[0] = 0
canvas.TopLeftBlue[0] = 0
canvas.TopLeftAlpha[0] = 1

local composite = add_tool("Merge", "ReviewRaster_Materialized", 0, 0)
composite.Background = canvas.Output
composite.Foreground = fit.Output
composite.PerformDepthMerge[0] = 0

local wip = add_tool(
    "ofx.com.jtorrens.WIPReviewProbe", "ReviewRaster_WIPReview", 2, 0)
wip.Source = composite.Output
set_input_by_id(wip, "canvasMode", 0)
set_input_by_id(wip, "placementMode", 0)
set_input_by_id(wip, "AllowResize", 0)
set_input_by_id(wip, "blankingEnabled", 0)
comp:Unlock()

-- BetterResize preserves the image aspect, so its output is not itself the
-- exact review canvas. The opaque Background + Merge materializes that canvas.
assert_dimensions(fit.Output, 192, 96, "centered fit")
assert_dimensions(composite.Output, 192, 108, "materialized review raster")
assert_dimensions(wip.Output, 192, 108, "WIP Review host-raster output")

print("OUTPUTPACKAGER_REVIEW_RASTER_HOST_TEST_OK")
return comp
