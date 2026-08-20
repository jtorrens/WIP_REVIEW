-- Saves the active test composition to a disposable path before UI close.

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end

local comp = fusion.CurrentComp
if comp == nil then error("Fusion has no active temporary composition") end

local temporary_path = "/private/tmp/codex_fusion_temporary.comp"
os.remove(temporary_path)
local saved = comp:Save(temporary_path)
if saved == false then error("Fusion could not save the temporary composition") end
print("FUSION_TEMP_COMP_READY_TO_CLOSE")
