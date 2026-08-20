-- Fusion Standalone 21 host probe for the native InputPrep building blocks.
-- Creates a private unsaved composition and closes it after inspection.

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end

local comp = fusion:NewComp()
if comp == nil then error("Fusion could not create the InputPrep probe comp") end
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local candidates = {
    "Background",
    "ChangeDepth",
    "BetterResize",
    "Resize",
    "Crop",
    "ColorSpaceTransform",
    "AlphaDivide",
    "AlphaMultiply",
    "ChannelBoolean",
    "ChannelBooleans",
    "MatteControl",
    "Dissolve",
    "Switch",
    "PipeRouter",
    "ColorBars",
    "Grid",
}

local interesting = {
    Width = true,
    Height = true,
    Depth = true,
    KeepAspect = true,
    FitMethod = true,
    InputColorSpace = true,
    InputGamma = true,
    OutputColorSpace = true,
    OutputGamma = true,
    Alpha = true,
    Operation = true,
    Red = true,
    Green = true,
    Blue = true,
    XOffset = true,
    YOffset = true,
    XSize = true,
    YSize = true,
    Center = true,
    Fit = true,
    Type = true,
    FilterMethod = true,
    WindowMethod = true,
    KeepCentered = true,
    ToRed = true,
    ToGreen = true,
    ToBlue = true,
    ToAlpha = true,
    ProcessAlpha = true,
    Mix = true,
    Which = true,
    Foreground = true,
    Background = true,
}

local function attr(input, name)
    local ok, attrs = pcall(function() return input:GetAttrs() end)
    if not ok or type(attrs) ~= "table" then return nil end
    return attrs[name]
end

local function value_text(value)
    if type(value) == "table" then
        local parts = {}
        for key, item in pairs(value) do
            parts[#parts + 1] = tostring(key) .. "=" .. tostring(item)
        end
        table.sort(parts)
        return "{" .. table.concat(parts, ",") .. "}"
    end
    return tostring(value)
end

local x = -4
for _, reg_id in ipairs(candidates) do
    local tool = comp:AddTool(reg_id, x, 0)
    if tool == nil then
        print("INPUTPREP_PROBE_TOOL_MISSING | " .. reg_id)
    else
        tool:SetAttrs({ TOOLS_Name = "Probe_" .. reg_id })
        local attrs = tool:GetAttrs() or {}
        print(string.format("INPUTPREP_PROBE_TOOL | requested=%s regid=%s name=%s",
            reg_id, tostring(attrs.TOOLS_RegID), tostring(attrs.TOOLS_Name)))
        local rows = {}
        for _, input in pairs(tool:GetInputList() or {}) do
            local id = attr(input, "INPS_ID")
            local name = attr(input, "INPS_Name")
            local id_text = tostring(id)
            local name_text = tostring(name)
            local relevant = reg_id == "Switch" or reg_id == "PipeRouter"
                or interesting[id_text] or interesting[name_text]
                or id_text:find("Size") or id_text:find("Offset")
                or id_text:find("Aspect") or id_text:find("Fit")
                or id_text:find("Center") or id_text:find("Channel")
            if relevant then
                local current_ok, current = pcall(function()
                    return input[comp.CurrentTime]
                end)
                rows[#rows + 1] = {
                    id = tostring(id),
                    text = string.format(
                        "INPUTPREP_PROBE_INPUT | tool=%s id=%s name=%s type=%s control=%s current=%s ids=%s strings=%s",
                        reg_id,
                        tostring(id),
                        tostring(name),
                        tostring(attr(input, "INPS_DataType")),
                        tostring(attr(input, "INPID_InputControl")),
                        current_ok and value_text(current) or "<unreadable>",
                        value_text(attr(input, "INPIDT_ComboControl_ID")),
                        value_text(attr(input, "INPST_ComboControl_String")))
                }
            end
        end
        table.sort(rows, function(a, b) return a.id < b.id end)
        for _, row in ipairs(rows) do print(row.text) end
        x = x + 1
    end
end

print("INPUTPREP_PROBE_READY")
return comp
