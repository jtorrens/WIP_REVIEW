local function dir()
    local s = debug.getinfo(1, "S").source:sub(2)
    return s:match("^(.*[/\\])")
end
local fusion = assert(bmd.scriptapp("Fusion", "localhost"))
local temp = dofile(dir() .. "../../test_support/temp_comp.lua")
local comp = assert(temp.acquire(fusion))
_G.comp = comp
local saver = assert(comp:AddTool("Saver", 1, 0))
saver:SetAttrs({ TOOLS_Name = "Saver_Versioned" })
saver.Clip[0] = "_FOQN:/old.mov"
dofile(dir() .. "../build_shot_config.lua")
local apply = dofile(dir() .. "../apply_shot_config.lua")
local config = assert(apply.find_config(comp))
local t = comp.CurrentTime or 0
config[apply.CONTROL.root][t] = "_FOQN:"
config[apply.CONTROL.show][t] = "FOQN"
config[apply.CONTROL.episode][t] = "E06"
config[apply.CONTROL.shot][t] = "0010"
config[apply.CONTROL.version][t] = "v002"
local node, template = apply.target_controls("Saver", 1)
config[node][t] = "Saver_Versioned"
config[template][t] = "{root}/{show}_{episode}/WIP/{show}_{episode}_{shot}_WIP_{version}.mov"
for index = 2, apply.TARGET_SLOT_COUNT do
    local unused_node, unused_template = apply.target_controls("Saver", index)
    config[unused_node][t] = ""
    config[unused_template][t] = ""
end
assert(apply.run_savers(comp))
assert(tostring(saver.Clip[t]):find("FOQN_E06_0010_WIP_v002.mov", 1, true) ~= nil)
print("SHOTCONFIG_SAVER_VERSIONS_HOST_TEST_OK")
return comp
