-- Pixel-host geometry validation for uniform centered fill in Fusion 21.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local geometry = dofile(script_directory() .. "../geometry.lua")
local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end

local temp_comp = dofile(script_directory() .. "../../test_support/temp_comp.lua")
local comp = temp_comp.acquire(fusion)
if comp == nil then error("Fusion could not create the resize test comp") end
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local function assert_equal(actual, expected, message)
    if actual ~= expected then
        error(string.format("%s: expected %s, got %s",
            message, tostring(expected), tostring(actual)))
    end
end

local function add_tool(reg_id, name, x, y)
    local tool = comp:AddTool(reg_id, x, y)
    if tool == nil then error("Fusion could not add " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    return tool
end

local function rendered_dimensions(output)
    local image = output[0]
    if image == nil then error("Fusion did not render the test image") end
    return tonumber(image.Width), tonumber(image.Height)
end

local cases = {
    { name = "square", width = 400, height = 400, resizeWidth = 384,
        resizeHeight = 384 },
    { name = "wide", width = 600, height = 200, resizeWidth = 648,
        resizeHeight = 216 },
    { name = "tall", width = 200, height = 600, resizeWidth = 384,
        resizeHeight = 1152 },
}

comp:Lock()
for index, case in ipairs(cases) do
    local source = add_tool("Background", "FillSource_" .. case.name, -2, index)
    source.Width[0] = case.width
    source.Height[0] = case.height

    local resize = add_tool("BetterResize", "FillResize_" .. case.name, 0, index)
    local fill_width = geometry.fill_width(
        case.width, case.height, 384, 216)
    resize.Width[0] = fill_width
    resize.Height[0] = 216
    resize.KeepAspect[0] = 1
    resize.FilterMethod[0] = 3
    resize.Input = source.Output

    local resize_width, resize_height = rendered_dimensions(resize.Output)
    assert_equal(fill_width, case.resizeWidth, case.name .. " fill width")
    assert_equal(resize_width, case.resizeWidth, case.name .. " rendered width")
    assert_equal(resize_height, case.resizeHeight, case.name .. " rendered height")
    if resize_width < 384 or resize_height < 216 then
        error(case.name .. " resize does not cover the working raster")
    end

    local crop = add_tool("Crop", "FillCrop_" .. case.name, 2, index)
    crop.XSize[0] = 384
    crop.YSize[0] = 192
    crop.XOffset[0] = 0
    crop.YOffset[0] = 0
    crop.KeepCentered[0] = 1
    crop.Input = resize.Output

    local crop_width, crop_height = rendered_dimensions(crop.Output)
    assert_equal(crop_width, 384, case.name .. " crop width")
    assert_equal(crop_height, 192, case.name .. " crop height")
end
comp:Unlock()

print("INPUTPREP_RESIZE_HOST_TEST_OK")
return comp
