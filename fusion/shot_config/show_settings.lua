-- Persists named ShowConfig definitions outside the installed scripts.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local SCRIPT_DIR = script_directory()
local apply = dofile(SCRIPT_DIR .. "apply_shot_config.lua")
local json = dofile(SCRIPT_DIR .. "color_enum_catalog.lua")
local refresh = dofile(SCRIPT_DIR .. "refresh_resolved_paths.lua")
local M = {}
M.SCHEMA_VERSION = 1

local function read(tool, id, time)
    local input = tool[id]
    if input == nil then return nil, "control '" .. id .. "' is missing" end
    return input[time]
end

local function write(tool, id, value, time)
    local input = tool[id]
    if input == nil then return false, "control '" .. id .. "' is missing" end
    input[time] = value
    return true
end

local function active_comp(tool, supplied_comp)
    if supplied_comp ~= nil then return supplied_comp end
    local ok, owner = pcall(function() return tool.Comp end)
    if ok then return owner end
    return nil
end

local function directory()
    local fusion = bmd.scriptapp("Fusion", "localhost")
    if fusion == nil then error("Fusion application is unavailable") end
    local path = fusion:MapPath("Profile:/ShowManager/Shows")
    if type(path) ~= "string" or path == "" then error("Fusion Profile path is unavailable") end
    path = path:gsub("/+$", "")
    local ok = os.execute("mkdir -p " .. string.format("%q", path))
    if ok ~= true and ok ~= 0 then error("unable to create Show Settings directory") end
    return path
end

local function definition_path(tool, time)
    local name, err = read(tool, apply.CONTROL.settings_name, time)
    if err ~= nil then error(err) end
    name = tostring(name):match("^%s*(.-)%s*$")
    if not name:match("^[%w_-]+$") then
        error("Definition Name may use only letters, numbers, '_' and '-'")
    end
    return directory() .. "/" .. name .. ".json", name
end

local function status(tool, message, time)
    pcall(function() write(tool, apply.CONTROL.status, message, time) end)
end

function M.save(tool, supplied_comp)
    local comp = active_comp(tool, supplied_comp)
    if comp == nil then error("active composition is unavailable") end
    local time = comp.CurrentTime or 0
    local path, name = definition_path(tool, time)
    local controls = {}
    for _, id in ipairs(apply.settings_control_names()) do
        local value, err = read(tool, id, time)
        if err ~= nil then error(err) end
        if type(value) == "table" then
            controls[id] = { value[1] or value.X, value[2] or value.Y }
        else
            controls[id] = value
        end
    end
    local file = assert(io.open(path .. ".tmp", "wb"))
    file:write(json.encode_json({ schemaVersion = M.SCHEMA_VERSION, controls = controls }))
    file:close()
    assert(os.rename(path .. ".tmp", path))
    status(tool, "OK: saved show settings '" .. name .. "'", time)
    return true, path
end

local function choose_settings_path(comp)
    local response = comp:AskUser("Load Show Settings", {
        {
            "SettingsFile",
            "FileBrowse",
            Default = directory() .. "/",
            FBC_Filter = "Show Settings (*.json)|*.json",
        },
    })
    if response == nil then return nil end
    local path = response.SettingsFile
    if type(path) ~= "string" or path == "" then return nil end
    return path
end

function M.load(tool, supplied_comp, selected_path)
    local comp = active_comp(tool, supplied_comp)
    if comp == nil then error("active composition is unavailable") end
    local time = comp.CurrentTime or 0
    local path = selected_path or choose_settings_path(comp)
    if path == nil then return false, "load cancelled" end
    local name = path:match("([^/\\]+)%.json$") or "settings"
    local file = io.open(path, "rb")
    if file == nil then error("show settings '" .. name .. "' do not exist") end
    local content = file:read("*a")
    file:close()
    local document = json.decode_json(content)
    if type(document) ~= "table" or tonumber(document.schemaVersion) ~= M.SCHEMA_VERSION or
        type(document.controls) ~= "table" then
        error("unsupported show settings file")
    end
    for _, id in ipairs(apply.settings_control_names()) do
        local value = document.controls[id]
        if value ~= nil then
            local ok, err = write(tool, id, value, time)
            if not ok then error(err) end
        end
    end
    local refreshed, refresh_error = refresh.run(tool, time)
    if not refreshed then error(refresh_error) end
    local name_written, name_error = write(tool, apply.CONTROL.settings_name, name, time)
    if not name_written then error(name_error) end
    status(tool, "OK: loaded show settings '" .. name .. "'", time)
    return true, path
end

return M
