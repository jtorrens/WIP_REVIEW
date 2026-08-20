-- Fusion 21 validation for the public WIP controls on OutputPackager.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local temp_comp = dofile(script_directory() .. "../../test_support/temp_comp.lua")
local comp = temp_comp.acquire(fusion)
if comp == nil then error("Fusion could not create the WIP UI test comp") end
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local function fail(message)
    error("OutputPackager WIP UI failure: " .. message, 2)
end

local builder = dofile(script_directory() .. "../build_output_packager.lua")
local group = builder.last_group
if group == nil then fail("builder returned no group") end

local controls = {
    "OP_BlankingEnabled", "OP_BlankingOpacity",
    "OP_FontFamily", "OP_FontSize", "OP_TextOpacity",
    "OP_OutlineEnabled", "OP_OutlineWidth",
    "OP_ShadowEnabled", "OP_ShadowOffsetX", "OP_ShadowOffsetY",
    "OP_ShadowSoftness", "OP_ShadowOpacity",
    "OP_TLEnabled", "OP_TLText", "OP_TLCalculatedField",
    "OP_TCEnabled", "OP_TCText", "OP_TCCalculatedField",
    "OP_TREnabled", "OP_TRText", "OP_TRCalculatedField",
    "OP_BLEnabled", "OP_BLText", "OP_BLCalculatedField",
    "OP_BCEnabled", "OP_BCText", "OP_BCCalculatedField",
    "OP_BREnabled", "OP_BRText", "OP_BRCalculatedField",
    "OP_FrameRelativeBase", "OP_FrameStart", "OP_FPSMode",
    "OP_FPSOverride", "OP_TimecodeStart", "OP_DropFrameMode",
    "OP_ColorSpaceMode", "OP_ManualColorSpace",
}
for _, id in ipairs(controls) do
    if group[id] == nil then fail("missing public control " .. id) end
end

group.OP_BlankingEnabled[0] = 1
group.OP_BlankingOpacity[0] = 0.75
group.OP_FontFamily[0] = "System Default"
group.OP_TLEnabled[0] = 1
group.OP_TLText[0] = "{frame} · "
group.OP_TLCalculatedField[0] = 2
group.OP_BREnabled[0] = 1
group.OP_BRText[0] = "{timecode}"
group.OP_FrameStart[0] = 1001
group.OP_FPSMode[0] = 1
group.OP_FPSOverride[0] = 25
group.OP_TimecodeStart[0] = "10:00:00:00"
group.OP_ColorSpaceMode[0] = 1
group.OP_ManualColorSpace[0] = 0

if tostring(group.OP_TLText[0]) ~= "{frame} · " then fail("Top Left text did not persist") end
if tostring(group.OP_BRText[0]) ~= "{timecode}" then fail("Bottom Right text did not persist") end
local combo_attrs = group.OP_TLCalculatedField:GetAttrs() or {}
if combo_attrs.INPID_InputControl ~= "ComboControl" then
    fail("Top Left Calculated Field is not exposed as a combo")
end
local combo_values = combo_attrs.INPST_ComboControl_String or {}
if combo_values[1] ~= "None" or combo_values[5] ~= "Date" then
    fail("Top Left Calculated Field combo values are not preserved")
end
if tonumber(group.OP_FPSOverride[0]) ~= 25 then fail("FPS Override did not persist") end
if tostring(group.OP_TimecodeStart[0]) ~= "10:00:00:00" then
    fail("Timecode Start did not persist")
end

local replacement = builder.rebuild(comp, group)
if tostring(replacement.OP_TLText[0]) ~= "{frame} · " then
    fail("rebuild lost Top Left text")
end
if tostring(replacement.OP_BRText[0]) ~= "{timecode}" then
    fail("rebuild lost Bottom Right text")
end
if tonumber(replacement.OP_FPSOverride[0]) ~= 25 then
    fail("rebuild lost FPS Override")
end

print("OUTPUTPACKAGER_WIP_UI_HOST_TEST_OK")
return comp
