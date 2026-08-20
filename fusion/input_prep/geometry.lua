-- Pure geometry helpers shared by the InputPrep builder and host tests.

local M = {}

local function positive(value, label)
    value = tonumber(value)
    if value == nil or value <= 0 then error(label .. " must be positive") end
    return value
end

function M.round_even(value)
    local rounded = math.floor(positive(value, "Dimension") + 0.5)
    if rounded % 2 ~= 0 then rounded = rounded - 1 end
    return math.max(2, rounded)
end

function M.ceil_even(value)
    local rounded = math.ceil(positive(value, "Dimension"))
    if rounded % 2 ~= 0 then rounded = rounded + 1 end
    return math.max(2, rounded)
end

function M.crop_dimensions(width, height, ratio)
    width = M.round_even(width)
    height = M.round_even(height)
    ratio = positive(ratio, "Crop Ratio")
    if width / height > ratio then
        width = M.round_even(height * ratio)
    else
        height = M.round_even(width / ratio)
    end
    return width, height
end

function M.fill_width(source_width, source_height, target_width, target_height)
    source_width = positive(source_width, "Source Width")
    source_height = positive(source_height, "Source Height")
    target_width = M.round_even(target_width)
    target_height = M.round_even(target_height)
    local width_required_by_height = target_height * source_width / source_height
    return M.ceil_even(math.max(target_width, width_required_by_height))
end

return M
