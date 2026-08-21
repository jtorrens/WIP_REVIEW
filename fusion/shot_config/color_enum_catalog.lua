-- Discovery and editable JSON catalogue for ColorSpaceTransform enums.

local M = {}
M.SCHEMA_VERSION = 1

local function directory_of_this_file()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local MODULE_DIR = directory_of_this_file()
M.DEFAULT_PATH = MODULE_DIR .. "color_enum_labels.json"

local function read_all(path)
    local file = io.open(path, "rb")
    if file == nil then return nil end
    local content = file:read("*a")
    file:close()
    return content
end

local function json_error(position, message)
    error(string.format("JSON error at byte %d: %s", position, message), 0)
end

local function decode_json(source)
    local position = 1
    local length = #source

    local function skip_space()
        while position <= length and source:sub(position, position):match("%s") do
            position = position + 1
        end
    end

    local parse_value
    local function parse_string()
        if source:sub(position, position) ~= '"' then
            json_error(position, "expected string")
        end
        position = position + 1
        local parts = {}
        local start = position
        while position <= length do
            local char = source:sub(position, position)
            if char == '"' then
                parts[#parts + 1] = source:sub(start, position - 1)
                position = position + 1
                return table.concat(parts)
            end
            if char == "\\" then
                parts[#parts + 1] = source:sub(start, position - 1)
                position = position + 1
                local escaped = source:sub(position, position)
                local replacements = {
                    ['"'] = '"', ["\\"] = "\\", ["/"] = "/",
                    b = "\b", f = "\f", n = "\n", r = "\r", t = "\t",
                }
                if replacements[escaped] ~= nil then
                    parts[#parts + 1] = replacements[escaped]
                    position = position + 1
                elseif escaped == "u" then
                    local hex = source:sub(position + 1, position + 4)
                    if not hex:match("^%x%x%x%x$") then
                        json_error(position, "invalid unicode escape")
                    end
                    local code = tonumber(hex, 16)
                    if code < 0x80 then
                        parts[#parts + 1] = string.char(code)
                    elseif code < 0x800 then
                        parts[#parts + 1] = string.char(
                            0xC0 + math.floor(code / 0x40),
                            0x80 + (code % 0x40))
                    else
                        parts[#parts + 1] = string.char(
                            0xE0 + math.floor(code / 0x1000),
                            0x80 + (math.floor(code / 0x40) % 0x40),
                            0x80 + (code % 0x40))
                    end
                    position = position + 5
                else
                    json_error(position, "invalid escape")
                end
                start = position
            elseif char:byte() < 32 then
                json_error(position, "control character in string")
            else
                position = position + 1
            end
        end
        json_error(position, "unterminated string")
    end

    local function parse_array()
        position = position + 1
        local result = {}
        skip_space()
        if source:sub(position, position) == "]" then
            position = position + 1
            return result
        end
        while true do
            result[#result + 1] = parse_value()
            skip_space()
            local char = source:sub(position, position)
            if char == "]" then
                position = position + 1
                return result
            end
            if char ~= "," then json_error(position, "expected ',' or ']'") end
            position = position + 1
            skip_space()
        end
    end

    local function parse_object()
        position = position + 1
        local result = {}
        skip_space()
        if source:sub(position, position) == "}" then
            position = position + 1
            return result
        end
        while true do
            local key = parse_string()
            skip_space()
            if source:sub(position, position) ~= ":" then
                json_error(position, "expected ':'")
            end
            position = position + 1
            skip_space()
            result[key] = parse_value()
            skip_space()
            local char = source:sub(position, position)
            if char == "}" then
                position = position + 1
                return result
            end
            if char ~= "," then json_error(position, "expected ',' or '}'") end
            position = position + 1
            skip_space()
        end
    end

    local function parse_number()
        local start = position
        local token = source:sub(position):match("^-?%d+%.?%d*[eE]?[+-]?%d*")
        if token == nil or token == "" then json_error(position, "invalid number") end
        position = position + #token
        local number = tonumber(token)
        if number == nil then json_error(start, "invalid number") end
        return number
    end

    parse_value = function()
        skip_space()
        local char = source:sub(position, position)
        if char == '"' then return parse_string() end
        if char == "[" then return parse_array() end
        if char == "{" then return parse_object() end
        if source:sub(position, position + 3) == "true" then
            position = position + 4; return true
        end
        if source:sub(position, position + 4) == "false" then
            position = position + 5; return false
        end
        if source:sub(position, position + 3) == "null" then
            position = position + 4; return nil
        end
        return parse_number()
    end

    local result = parse_value()
    skip_space()
    if position <= length then json_error(position, "trailing content") end
    return result
end

local function escape_json(value)
    local escaped = tostring(value):gsub('[%z\1-\31\\"]', function(char)
        local replacements = {
            ['"'] = '\\"', ["\\"] = "\\\\", ["\b"] = "\\b",
            ["\f"] = "\\f", ["\n"] = "\\n", ["\r"] = "\\r",
            ["\t"] = "\\t",
        }
        return replacements[char] or string.format("\\u%04x", char:byte())
    end)
    return '"' .. escaped .. '"'
end

function M.decode_json(source)
    return decode_json(source)
end

function M.encode_json(value)
    local function encode(item)
        local item_type = type(item)
        if item_type == "string" then return escape_json(item) end
        if item_type == "number" then return tostring(item) end
        if item_type == "boolean" then return item and "true" or "false" end
        if item_type ~= "table" then error("JSON cannot encode " .. item_type) end
        local count, is_array = 0, true
        for key in pairs(item) do
            count = count + 1
            if type(key) ~= "number" or key < 1 or key % 1 ~= 0 then is_array = false end
        end
        if is_array then
            local values = {}
            for index = 1, count do values[#values + 1] = encode(item[index]) end
            return "[" .. table.concat(values, ",") .. "]"
        end
        local keys, values = {}, {}
        for key in pairs(item) do keys[#keys + 1] = tostring(key) end
        table.sort(keys)
        for _, key in ipairs(keys) do
            values[#values + 1] = escape_json(key) .. ":" .. encode(item[key])
        end
        return "{" .. table.concat(values, ",") .. "}"
    end
    return encode(value) .. "\n"
end

local function encode_entries(entries, indent)
    local lines = { "[" }
    for index, entry in ipairs(entries) do
        lines[#lines + 1] = string.format(
            "%s  { \"id\": %s, \"label\": %s }%s",
            indent, escape_json(entry.id), escape_json(entry.label),
            index < #entries and "," or "")
    end
    lines[#lines + 1] = indent .. "]"
    return table.concat(lines, "\n")
end

local function encode_catalog(catalog)
    return table.concat({
        "{",
        "  \"schemaVersion\": " .. tostring(M.SCHEMA_VERSION) .. ",",
        "  \"fusionVersion\": " .. escape_json(catalog.fusionVersion or "unknown") .. ",",
        "  \"colorSpaces\": " .. encode_entries(catalog.colorSpaces, "  ") .. ",",
        "  \"gammas\": " .. encode_entries(catalog.gammas, "  "),
        "}",
        "",
    }, "\n")
end

local function validate_entries(entries, name)
    if type(entries) ~= "table" then error(name .. " must be an array") end
    local seen = {}
    for index, entry in ipairs(entries) do
        if type(entry) ~= "table" or type(entry.id) ~= "string" or
           entry.id == "" or type(entry.label) ~= "string" or entry.label == "" then
            error(string.format("%s[%d] must contain non-empty id and label strings",
                name, index))
        end
        if seen[entry.id] then error(name .. " contains duplicate id " .. entry.id) end
        seen[entry.id] = true
    end
end

local function validate_catalog(catalog)
    if type(catalog) ~= "table" then error("catalog root must be an object") end
    if tonumber(catalog.schemaVersion) ~= M.SCHEMA_VERSION then
        error("unsupported catalog schema")
    end
    validate_entries(catalog.colorSpaces, "colorSpaces")
    validate_entries(catalog.gammas, "gammas")
    return catalog
end

local function load_catalog(path)
    local content = read_all(path)
    if content == nil then return nil, "missing" end
    local ok, result = pcall(function()
        return validate_catalog(decode_json(content))
    end)
    if not ok then return nil, tostring(result) end
    return result
end

local function atomic_write(path, content)
    local temporary = path .. ".tmp"
    local file, err = io.open(temporary, "wb")
    if file == nil then error("unable to write " .. temporary .. ": " .. tostring(err)) end
    local ok, failure = pcall(function()
        file:write(content)
        file:flush()
        file:close()
    end)
    if not ok then
        pcall(function() file:close() end)
        os.remove(temporary)
        error(failure)
    end
    local renamed, rename_error = os.rename(temporary, path)
    if not renamed then
        os.remove(temporary)
        error("unable to replace " .. path .. ": " .. tostring(rename_error))
    end
end

local function labels_by_id(entries)
    local result = {}
    for _, entry in ipairs(entries or {}) do result[entry.id] = entry.label end
    return result
end

local function reconcile_entries(discovered_ids, existing, seed)
    local discovered = {}
    for _, id in ipairs(discovered_ids) do discovered[id] = true end
    local existing_labels = labels_by_id(existing)
    local seed_labels = labels_by_id(seed)
    local result, added = {}, 0
    local included = {}

    local function include(id)
        if discovered[id] and not included[id] then
            result[#result + 1] = {
                id = id,
                label = existing_labels[id] or seed_labels[id] or id,
            }
            included[id] = true
        end
    end
    for _, entry in ipairs(seed or {}) do include(entry.id) end
    for _, entry in ipairs(existing or {}) do include(entry.id) end
    for _, id in ipairs(discovered_ids) do
        if not included[id] then added = added + 1 end
        include(id)
    end
    return result, added
end

function M.reconcile(discovery, existing, seed, fusion_version)
    local color_spaces, added_spaces = reconcile_entries(
        discovery.colorSpaces, existing.colorSpaces, seed.colorSpaces)
    local gammas, added_gammas = reconcile_entries(
        discovery.gammas, existing.gammas, seed.gammas)
    return {
        schemaVersion = M.SCHEMA_VERSION,
        fusionVersion = fusion_version,
        colorSpaces = color_spaces,
        gammas = gammas,
    }, added_spaces, added_gammas
end

local function input_options(tool, input_id)
    local input = tool[input_id]
    if input == nil then error("ColorSpaceTransform input missing: " .. input_id) end
    local attrs = input:GetAttrs() or {}
    local options = attrs.INPIDT_ComboControl_ID
    if type(options) ~= "table" then
        error("ColorSpaceTransform enum unavailable: " .. input_id)
    end
    local keys = {}
    for key in pairs(options) do keys[#keys + 1] = key end
    table.sort(keys, function(a, b) return tonumber(a) < tonumber(b) end)
    local result, seen = {}, {}
    for _, key in ipairs(keys) do
        local id = tostring(options[key])
        if id ~= "" and not seen[id] then
            result[#result + 1] = id
            seen[id] = true
        end
    end
    if #result == 0 then error("ColorSpaceTransform enum is empty: " .. input_id) end
    return result
end

function M.discover(fusion)
    if fusion == nil then error("Fusion application is unavailable") end
    local method_ok, new_comp = pcall(function() return fusion.NewComp end)
    if not method_ok or new_comp == nil then
        error("Fusion application cannot create the enum probe composition")
    end
    local previous = fusion.CurrentComp
    local probe = nil
    for _ = 1, 30 do
        probe = fusion:NewComp()
        if probe ~= nil then break end
        if rawget(_G, "bmd") ~= nil then bmd.wait(1) end
    end
    if probe == nil then error("unable to create private enum probe composition") end
    local result
    local ok, failure = pcall(function()
        probe:Lock()
        local cst = probe:AddTool("ColorSpaceTransform", 0, 0)
        if cst == nil then error("native ColorSpaceTransform tool is unavailable") end
        result = {
            colorSpaces = input_options(cst, "InputColorSpace"),
            gammas = input_options(cst, "InputGamma"),
        }
        probe:Unlock()
    end)
    pcall(function() probe:Lock() end)
    pcall(function() probe:Close() end)
    if previous ~= nil then pcall(function() fusion:SetActiveComp(previous) end) end
    if not ok then error(failure) end
    return result
end

function M.sync(fusion, path_override)
    local path = path_override or M.DEFAULT_PATH
    local discovery = M.discover(fusion)
    local existing, load_error = load_catalog(path)
    local seed = dofile(MODULE_DIR .. "color_enum_seed.lua")
    if existing == nil then
        existing = { colorSpaces = {}, gammas = {} }
        if load_error ~= "missing" then
            print("[ShotConfig] WARNING: malformed color enum JSON; regenerating seed")
        end
    end
    local attrs = fusion:GetAttrs() or {}
    local version = tostring(attrs.FUSIONS_Version or attrs.FUSIONS_VersionString or "unknown")
    local catalog, added_spaces, added_gammas =
        M.reconcile(discovery, existing, seed, version)
    atomic_write(path, encode_catalog(catalog))
    print(string.format(
        "[ShotConfig] Color enums synchronized: %d spaces, %d gammas (+%d/+%d)",
        #catalog.colorSpaces, #catalog.gammas, added_spaces, added_gammas))
    return catalog
end

function M.load(path_override)
    local catalog, err = load_catalog(path_override or M.DEFAULT_PATH)
    if catalog == nil then return nil, err end
    return catalog
end

return M
