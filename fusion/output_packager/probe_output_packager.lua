-- Fusion Standalone 21 host probe for OutputPackager building blocks.
-- Creates a private unsaved composition and closes it after inspection.

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end

local comp = fusion:NewComp()
if comp == nil then error("Fusion could not create the OutputPackager probe comp") end
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local candidates = {
    { reg_id = "Background", required = true },
    { reg_id = "BetterResize", required = true },
    { reg_id = "Merge", required = true },
    { reg_id = "Switch", required = true },
    { reg_id = "Saver", required = true },
    { reg_id = "ofx.com.jtorrens.WIPReviewProbe", required = true },
    { reg_id = "ofx.com.jtorrens.WIPReviewProbe.Filter", required = false },
}

local interesting = {
    Width = true,
    Height = true,
    KeepAspect = true,
    KeepCentered = true,
    Source = true,
    Foreground = true,
    Background = true,
    Which = true,
    Clip = true,
    ProcessMode = true,
    OutputFormat = true,
    CreateDir = true,
    Blend = true,
    Input = true,
    requestCustomRoD = true,
    requestedWidth = true,
    requestedHeight = true,
    AllowResize = true,
    canvasMode = true,
    placementMode = true,
    resampleFilter = true,
    blankingEnabled = true,
    blankingAspectPreset = true,
    blankingAspectCustom = true,
    blankingOpacity = true,
    colorSpaceMode = true,
    manualColorSpace = true,
    frameRelativeBase = true,
    frameStart = true,
    fpsMode = true,
    fpsOverride = true,
    timecodeStart = true,
    dropFrameMode = true,
    topLeftText = true,
    topCenterText = true,
    topRightText = true,
    bottomLeftText = true,
    bottomCenterText = true,
    bottomRightText = true,
}

local function input_attr(input, name)
    local ok, attrs = pcall(function() return input:GetAttrs() end)
    if not ok or type(attrs) ~= "table" then return nil end
    return attrs[name]
end

local function value_text(value)
    if type(value) ~= "table" then return tostring(value) end
    local parts = {}
    for key, item in pairs(value) do
        parts[#parts + 1] = tostring(key) .. "=" .. tostring(item)
    end
    table.sort(parts)
    return "{" .. table.concat(parts, ",") .. "}"
end

local x = -3
for _, candidate in ipairs(candidates) do
    local requested_reg_id = candidate.reg_id
    local tool = comp:AddTool(requested_reg_id, x, 0)
    if tool == nil then
        local marker = candidate.required and "OUTPUTPACKAGER_PROBE_REQUIRED_MISSING"
            or "OUTPUTPACKAGER_PROBE_OPTIONAL_MISSING"
        print(marker .. " | " .. requested_reg_id)
    else
        tool:SetAttrs({ TOOLS_Name = "Probe_" .. requested_reg_id:gsub("[^%w]", "_") })
        local attrs = tool:GetAttrs() or {}
        print(string.format(
            "OUTPUTPACKAGER_PROBE_TOOL | requested=%s regid=%s name=%s",
            requested_reg_id,
            tostring(attrs.TOOLS_RegID),
            tostring(attrs.TOOLS_Name)))

        local rows = {}
        for _, input in pairs(tool:GetInputList() or {}) do
            local id = tostring(input_attr(input, "INPS_ID"))
            local name = tostring(input_attr(input, "INPS_Name"))
            if interesting[id] or interesting[name] then
                local current_ok, current = pcall(function()
                    return input[comp.CurrentTime]
                end)
                rows[#rows + 1] = {
                    id = id,
                    text = string.format(
                        "OUTPUTPACKAGER_PROBE_INPUT | tool=%s id=%s name=%s type=%s control=%s current=%s ids=%s strings=%s",
                        requested_reg_id,
                        id,
                        name,
                        tostring(input_attr(input, "INPS_DataType")),
                        tostring(input_attr(input, "INPID_InputControl")),
                        current_ok and value_text(current) or "<unreadable>",
                        value_text(input_attr(input, "INPIDT_ComboControl_ID")),
                        value_text(input_attr(input, "INPST_ComboControl_String")))
                }
            end
        end
        table.sort(rows, function(a, b) return a.id < b.id end)
        for _, row in ipairs(rows) do print(row.text) end
        x = x + 1
    end
end

print("OUTPUTPACKAGER_PROBE_READY")
return comp
