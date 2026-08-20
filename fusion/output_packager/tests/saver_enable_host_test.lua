-- Fusion 21 validation for native Saver enable/disable behavior.

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end

local comp = fusion:NewComp()
if comp == nil then error("Fusion could not create the Saver enable test comp") end
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local enabled_path = "/private/tmp/outputpackager_saver_enabled.exr"
local disabled_path = "/private/tmp/outputpackager_saver_disabled.exr"
local enabled_output = "/private/tmp/outputpackager_saver_enabled0000.exr"
local disabled_output = "/private/tmp/outputpackager_saver_disabled0000.exr"
os.remove(enabled_output)
os.remove(disabled_output)

local function fail(message)
    error("OutputPackager Saver enable failure: " .. message, 2)
end

local function add_tool(reg_id, name, x, y)
    local tool = comp:AddTool(reg_id, x, y)
    if tool == nil then fail("unable to add " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    return tool
end

local function file_exists(path)
    local handle = io.open(path, "rb")
    if handle == nil then return false end
    handle:close()
    return true
end

comp:Lock()
local source = add_tool("Background", "SaverEnable_Source", -2, 0)
source.Width[0] = 16
source.Height[0] = 16
source.TopLeftRed[0] = 0.25
source.TopLeftGreen[0] = 0.5
source.TopLeftBlue[0] = 0.75
source.TopLeftAlpha[0] = 1

local enabled = add_tool("Saver", "SaverEnable_Enabled", 0, 0)
enabled.Clip[0] = enabled_path
enabled.Input = source.Output

local disabled = add_tool("Saver", "SaverEnable_Disabled", 0, 1)
disabled.Clip[0] = disabled_path
disabled.Input = source.Output
disabled:SetAttrs({ TOOLB_PassThrough = true })
comp:Unlock()

local disabled_attrs = disabled:GetAttrs() or {}
if disabled_attrs.TOOLB_PassThrough ~= true then
    fail("TOOLB_PassThrough did not persist")
end

local render_ok = comp:Render({ Start = 0, End = 0, Wait = true })
if render_ok == false then fail("Fusion render failed") end
if not file_exists(enabled_output) then fail("enabled Saver created no file") end
if file_exists(disabled_output) then fail("disabled Saver created a file") end

print("OUTPUTPACKAGER_SAVER_ENABLE_HOST_TEST_OK")
return comp
