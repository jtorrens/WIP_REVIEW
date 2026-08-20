-- Reuses Fusion's automatic empty composition instead of accumulating tabs.

local M = {}

local function is_empty_unsaved(comp)
    if comp == nil then return false end
    local attrs_ok, attrs = pcall(function() return comp:GetAttrs() end)
    if not attrs_ok then return false end
    attrs = attrs or {}
    if tostring(attrs.COMPS_FileName or "") ~= "" then return false end
    local tools_ok, tool_list = pcall(function() return comp:GetToolList(false) end)
    if not tools_ok then return false end
    for _ in pairs(tool_list or {}) do return false end
    return true
end

function M.acquire(fusion)
    local current = fusion.CurrentComp
    if is_empty_unsaved(current) then return current end
    return fusion:NewComp()
end

return M
