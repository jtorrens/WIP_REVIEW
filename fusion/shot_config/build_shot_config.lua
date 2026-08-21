-- ShotConfig v0.1 development builder for Fusion 21.
-- Run with: dofile("/absolute/path/to/build_shot_config.lua")

local M = {}

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local SCRIPT_DIR = script_directory()
local APPLY_PATH = SCRIPT_DIR .. "apply_shot_config.lua"
local REBUILD_PATH = SCRIPT_DIR .. "rebuild_managed_pipeline.lua"
local LIVE_PREVIEW_PATH = SCRIPT_DIR .. "refresh_resolved_paths.lua"
local SHOW_SETTINGS_PATH = SCRIPT_DIR .. "show_settings.lua"
local apply = dofile(APPLY_PATH)
local color_catalog = dofile(SCRIPT_DIR .. "color_enum_catalog.lua")
local CONTROL = apply.CONTROL

local COLOR_DEFAULTS = {
    sourceColorSpace = "REC709_COLORSPACE",
    sourceGamma = "TWOPOINTFOUR_GAMMA",
    workingColorSpace = "REC709_COLORSPACE",
    workingGamma = "LINEAR_GAMMA",
}

local DEFAULTS = {
    [CONTROL.show] = "SHOW",
    [CONTROL.episode] = "E01",
    [CONTROL.shot] = "0010",
    [CONTROL.version] = "v001",
    [CONTROL.root] = "_SHOW:",
    [CONTROL.working_resolution] = { 3840, 2160 },
    [CONTROL.crop_ratio] = 2.0,
    [CONTROL.review_resolution] = { 1920, 1080 },
    [CONTROL.embedded_alpha] = 0,
    [CONTROL.settings_name] = "SHOW",
    [CONTROL.status] = "Ready",
}

local TARGET_TEMPLATE_DEFAULTS = {
    Loader = "{root}/{show}_{episode}/BRUTOS/{show}_{episode}_{shot}.mov",
    Saver = "{root}/{show}_{episode}/RENDERS/{show}_{episode}_{shot}_{version}.mov",
}

local SAVER_SEED = {
    { name = "Comp", template = "{root}/{show}_{episode}/COMP/{show}_{episode}_{shot}_COMP_{version}.mov" },
    { name = "WIP", template = "{root}/{show}_{episode}/WIP/{show}_{episode}_{shot}_WIP_{version}.mov" },
    { name = "GFX", template = "{root}/{show}_{episode}/GFX/{show}_{episode}_{shot}_GFX_{version}.mov" },
    { name = "Final", template = "{root}/{show}_{episode}/FINAL/{show}_{episode}_{shot}_FINAL_{version}.mov" },
}

local PRESERVED_CONTROLS = {
    CONTROL.show,
    CONTROL.episode,
    CONTROL.shot,
    CONTROL.version,
    CONTROL.root,
    CONTROL.working_resolution,
    CONTROL.crop_ratio,
    CONTROL.review_resolution,
    CONTROL.embedded_alpha,
    CONTROL.settings_name,
}
for _, kind in ipairs({ "Loader", "Saver" }) do
    for index = 1, apply.TARGET_SLOT_COUNT do
        local node_control, template_control, resolved_control, color_control,
            gamma_control, format_control, compression_control =
            apply.target_controls(kind, index)
        PRESERVED_CONTROLS[#PRESERVED_CONTROLS + 1] = node_control
        PRESERVED_CONTROLS[#PRESERVED_CONTROLS + 1] = template_control
        PRESERVED_CONTROLS[#PRESERVED_CONTROLS + 1] = color_control
        PRESERVED_CONTROLS[#PRESERVED_CONTROLS + 1] = gamma_control
        DEFAULTS[node_control] = ""
        DEFAULTS[template_control] = TARGET_TEMPLATE_DEFAULTS[kind]
        DEFAULTS[resolved_control] = ""
        DEFAULTS[color_control] = kind == "Loader" and 0 or 0
        DEFAULTS[gamma_control] = kind == "Loader" and 0 or 0
        if kind == "Saver" then
            PRESERVED_CONTROLS[#PRESERVED_CONTROLS + 1] = format_control
            PRESERVED_CONTROLS[#PRESERVED_CONTROLS + 1] = compression_control
            DEFAULTS[format_control] = 0
            DEFAULTS[compression_control] = "ProRes 422 HQ"
            local seed = SAVER_SEED[index]
            if seed ~= nil then
                DEFAULTS[node_control] = seed.name
                DEFAULTS[template_control] = seed.template
            end
        end
    end
end

local function log(message)
    print("[ShotConfig] " .. message)
end

local function text_control(id, label, default, lines, read_only, page, on_change)
    return string.format([[
        %s = {
            LINKS_Name = %q,
            LINKID_DataType = "Text",
            INPID_InputControl = "TextEditControl",
            INP_Default = %q,
            TEC_Lines = %d,
            TEC_Wrap = %s,
            TEC_ReadOnly = %s,
            INPS_ExecuteOnChange = %s,
            ICS_ControlPage = %q,
        },]], id, label, default, lines or 1,
        (lines or 1) > 1 and "true" or "false",
        read_only and "true" or "false", on_change and string.format("%q", on_change) or "nil", page)
end

local function point_control(id, label, x, y, page, on_change)
    return string.format([[
        %s = {
            LINKS_Name = %q,
            LINKID_DataType = "Point",
            INPID_InputControl = "OffsetControl",
            INPID_PreviewControl = "",
            INP_DefaultX = %s,
            INP_DefaultY = %s,
            INPS_ExecuteOnChange = %s,
            ICS_ControlPage = %q,
        },]], id, label, tostring(x), tostring(y),
        on_change and string.format("%q", on_change) or "nil", page)
end

local function number_control(id, label, default, minimum, maximum, page, on_change)
    return string.format([[
        %s = {
            LINKS_Name = %q,
            LINKID_DataType = "Number",
            INPID_InputControl = "SliderControl",
            INP_Default = %s,
            INP_MinScale = %s,
            INP_MaxScale = %s,
            INP_MinAllowed = %s,
            INP_MaxAllowed = %s,
            INPS_ExecuteOnChange = %s,
            ICS_ControlPage = %q,
        },]], id, label, tostring(default), tostring(minimum), tostring(maximum),
        tostring(minimum), tostring(maximum),
        on_change and string.format("%q", on_change) or "nil", page)
end

local function checkbox_control(id, label, default, page, on_change)
    return string.format([[
        %s = {
            LINKS_Name = %q,
            LINKID_DataType = "Number",
            INPID_InputControl = "CheckboxControl",
            INP_Default = %s,
            INP_Integer = true,
            INP_MinAllowed = 0,
            INP_MaxAllowed = 1,
            INPS_ExecuteOnChange = %s,
            ICS_ControlPage = %q,
        },]], id, label, tostring(default),
        on_change and string.format("%q", on_change) or "nil", page)
end

local function button_control(id, label, execute, page)
    return string.format([[
        %s = {
            LINKS_Name = %q,
            LINKID_DataType = "Number",
            INPID_InputControl = "ButtonControl",
            BTNCS_Execute = %q,
            ICS_ControlPage = %q,
        },]], id, label, execute, page)
end

local function label_control(id, label, page)
    return string.format([[
        %s = {
            LINKS_Name = %q,
            LINKID_DataType = "Text",
            INPID_InputControl = "LabelControl",
            INP_External = false,
            INP_Passive = true,
            ICS_ControlPage = %q,
        },]], id, label, page)
end

local function collapsible_control(id, label, child_count, page)
    return string.format([[
        %s = {
            LINKS_Name = %q,
            LINKID_DataType = "Number",
            INPID_InputControl = "LabelControl",
            INP_Default = 0,
            INP_Integer = true,
            INP_External = false,
            LBLC_DropDownButton = true,
            LBLC_NumInputs = %d,
            ICS_ControlPage = %q,
        },]], id, label, child_count, page)
end

local function combo_control(id, label, entries, default_index, page, on_change)
    local options = {}
    for _, entry in ipairs(entries) do
        options[#options + 1] = string.format("{ CCS_AddString = %q },", entry.label)
    end
    return string.format([[
        %s = {
            %s
            LINKS_Name = %q,
            LINKID_DataType = "Number",
            INPID_InputControl = "ComboControl",
            INP_Default = %d,
            INP_Integer = true,
            INP_MinAllowed = 0,
            INP_MaxAllowed = %d,
            INPS_ExecuteOnChange = %s,
            ICS_ControlPage = %q,
        },]], id, table.concat(options, "\n            "), label,
        default_index, math.max(0, #entries - 1),
        on_change and string.format("%q", on_change) or "nil", page)
end

local function serialized_controls(catalog, selections)
    local result = {}
    local function add(value) result[#result + 1] = value end
    local refresh_preview = string.format("dofile(%q).run(tool)", LIVE_PREVIEW_PATH)
    add(label_control("SC_IdentitySection", "Identity", "Shot"))
    add(text_control(CONTROL.show, "Show", DEFAULTS[CONTROL.show], 1, false, "Shot", refresh_preview))
    add(text_control(CONTROL.episode, "Episode", DEFAULTS[CONTROL.episode], 1, false, "Shot", refresh_preview))
    add(text_control(CONTROL.shot, "Shot", DEFAULTS[CONTROL.shot], 1, false, "Shot", refresh_preview))
    add(text_control(CONTROL.version, "Version", DEFAULTS[CONTROL.version], 1, false, "Shot", refresh_preview))
    add(label_control("SC_FormatSection", "Format", "Shot"))
    add(point_control(CONTROL.working_resolution, "Working Resolution",
        DEFAULTS[CONTROL.working_resolution][1], DEFAULTS[CONTROL.working_resolution][2], "Shot", refresh_preview))
    add(number_control(CONTROL.crop_ratio, "Crop Ratio", DEFAULTS[CONTROL.crop_ratio],
        0.1, 10.0, "Shot", refresh_preview))
    add(point_control(CONTROL.review_resolution, "Review Resolution",
        DEFAULTS[CONTROL.review_resolution][1], DEFAULTS[CONTROL.review_resolution][2], "Shot", refresh_preview))
    add(label_control("SC_FormatHelp",
        "Stored only; not applied to image nodes in v0.1.", "Shot"))
    add(combo_control(CONTROL.source_color_space_choice, "Source Color Space",
        catalog.colorSpaces, selections.sourceColorSpace, "Color", refresh_preview))
    add(combo_control(CONTROL.source_gamma_choice, "Source Gamma",
        catalog.gammas, selections.sourceGamma, "Color", refresh_preview))
    add(combo_control(CONTROL.working_color_space_choice, "Working Color Space",
        catalog.colorSpaces, selections.workingColorSpace, "Color", refresh_preview))
    add(combo_control(CONTROL.working_gamma_choice, "Working Gamma",
        catalog.gammas, selections.workingGamma, "Color", refresh_preview))
    add(checkbox_control(CONTROL.embedded_alpha, "Embedded Alpha",
        DEFAULTS[CONTROL.embedded_alpha], "Color", refresh_preview))
    add(label_control("SC_ColorHelp",
        "Stored only; not applied to CST nodes in v0.1.", "Color"))
    local execute = string.format("local m = dofile(%q); m.run(comp)", APPLY_PATH)
    local update_savers = string.format("local m = dofile(%q); m.run_savers(comp)", APPLY_PATH)
    add(label_control("SC_PathMapSection", "Path Map", "Targets"))
    add(text_control(CONTROL.root, "Root Path Map", DEFAULTS[CONTROL.root], 1, false, "Targets", refresh_preview))
    add(label_control("SC_PathMapHelp",
        "Portable Path Map, for example _SHOW:", "Targets"))
    add(button_control("SC_Apply", "Apply / Update", execute, "Targets"))
    add(button_control("SC_UpdateSaverVersions", "Update Saver Versions", update_savers, "Targets"))
    add(button_control("SC_RebuildPipeline", "Rebuild Managed Pipeline",
        string.format("local m = dofile(%q); m.run(comp)", REBUILD_PATH), "Targets"))
    add(label_control("SC_ShowSettingsSection", "Show Settings", "Targets"))
    add(text_control(CONTROL.settings_name, "Definition Name",
        DEFAULTS[CONTROL.settings_name], 1, false, "Targets"))
    add(button_control("SC_SaveShowSettings", "Save Settings",
        string.format("local m = dofile(%q); m.save(tool)", SHOW_SETTINGS_PATH), "Targets"))
    add(button_control("SC_LoadShowSettings", "Load Settings",
        string.format("local m = dofile(%q); m.load(tool)", SHOW_SETTINGS_PATH), "Targets"))
    add(text_control(CONTROL.status, "Status", DEFAULTS[CONTROL.status],
        1, true, "Targets"))
    add(label_control("SC_TargetHelp",
        "F2 copies the node name. Empty slots are ignored.",
        "Targets"))
    for _, kind in ipairs({ "Loader", "Saver" }) do
        local child_count = kind == "Loader" and 5 or 7
        add(collapsible_control("SC_" .. kind .. "Group", kind .. "s",
            apply.TARGET_SLOT_COUNT * (child_count + 1), "Targets"))
        for index = 1, apply.TARGET_SLOT_COUNT do
            local node_control, template_control, resolved_control, color_control,
                gamma_control, format_control, compression_control =
                apply.target_controls(kind, index)
            add(collapsible_control("SC_" .. kind .. "Target" .. index .. "Group",
                string.format("%d · %s", index, kind), child_count, "Targets"))
            add(text_control(node_control,
                string.format("%d · Node Name", index),
                DEFAULTS[node_control], 1, false, "Targets"))
            add(text_control(template_control,
                string.format("%d · Path Template", index),
                DEFAULTS[template_control], 1, false, "Targets", refresh_preview))
            add(text_control(resolved_control,
                string.format("%d · Resolved Path", index),
                DEFAULTS[resolved_control], 1, true, "Targets"))
            if kind == "Loader" then
                add(combo_control(color_control, "Source Color Space",
                    catalog.colorSpaces, selections.sourceColorSpace, "Targets"))
                add(combo_control(gamma_control, "Source Gamma",
                    catalog.gammas, selections.sourceGamma, "Targets"))
            else
                add(combo_control(color_control, "Output Color Space",
                    catalog.colorSpaces, selections.workingColorSpace, "Targets"))
                add(combo_control(gamma_control, "Output Gamma",
                    catalog.gammas, selections.workingGamma, "Targets"))
                add(combo_control(format_control, "Output Format", {
                    { label = "Movie" }, { label = "Image Sequence" },
                }, DEFAULTS[format_control], "Targets"))
                add(text_control(compression_control, "Compression",
                    DEFAULTS[compression_control], 1, false, "Targets"))
            end
        end
    end
    return table.concat(result, "\n")
end

local function create_group(comp, catalog, selections)
    local suffix = os.time()
    local name = "G_ShotConfigBuild_" .. tostring(suffix)
    while comp:FindTool(name) ~= nil do
        suffix = suffix + 1
        name = "G_ShotConfigBuild_" .. tostring(suffix)
    end
    local source = string.format([[
    {
        Tools = ordered() {
            %s = GroupOperator {
                UserControls = ordered() {
                    %s
                },
                ViewInfo = GroupInfo { Pos = { -32768, -32768 } },
            },
        },
        ActiveTool = %q,
    }
    ]], name, serialized_controls(catalog, selections), name)
    local parsed = bmd.readstring(source)
    if parsed == nil then error("Fusion could not parse ShotConfig definition") end
    local pasted = comp:Paste(parsed)
    if pasted == false then error("Fusion could not paste ShotConfig definition") end
    local group = comp:FindTool(name)
    if group == nil then error("pasted ShotConfig GroupOperator was not found") end
    return group
end

local function get_metadata(tool, key)
    local ok, value = pcall(function()
        return tool:GetData("ShotConfig." .. key)
    end)
    if ok then return value end
    return nil
end

local function role_matches(comp)
    local matches = {}
    local tools = comp:GetToolList(false) or {}
    for _, tool in pairs(tools) do
        if get_metadata(tool, "Role") == apply.ROLE then
            matches[#matches + 1] = tool
        end
    end
    return matches
end

local function find_input(tool, name)
    if tool == nil then return nil end
    local ok, value = pcall(function() return tool[name] end)
    if ok then return value end
    return nil
end

local function read_value(tool, name, time)
    local input = find_input(tool, name)
    if input == nil then return nil end
    local value_ok, value = pcall(function() return input[time] end)
    if value_ok then return value end
    return nil
end

local function capture_values(tool, time)
    local values = {}
    for _, name in ipairs(PRESERVED_CONTROLS) do
        local value = read_value(tool, name, time)
        if value ~= nil then values[name] = value end
    end
    return values
end

local function split_ids(value)
    local result = {}
    local normalized = tostring(value or ""):gsub("\r\n", "\n"):gsub("\r", "\n")
    for line in (normalized .. "\n"):gmatch("(.-)\n") do
        local id = line:match("^%s*(.-)%s*$")
        if id ~= "" then result[#result + 1] = id end
    end
    return result
end

local function selected_id(tool, choice_control, ids_key, time)
    local choice = tonumber(read_value(tool, choice_control, time))
    if choice == nil or choice % 1 ~= 0 then return nil end
    local ids = split_ids(get_metadata(tool, ids_key))
    return ids[choice + 1]
end

local function capture_color_selection(tool, time)
    return {
        sourceColorSpace = selected_id(tool, CONTROL.source_color_space_choice,
            apply.DATA.color_space_ids, time),
        sourceGamma = selected_id(tool, CONTROL.source_gamma_choice,
            apply.DATA.gamma_ids, time),
        workingColorSpace = selected_id(tool, CONTROL.working_color_space_choice,
            apply.DATA.color_space_ids, time),
        workingGamma = selected_id(tool, CONTROL.working_gamma_choice,
            apply.DATA.gamma_ids, time),
    }
end

local function index_by_id(entries, requested_id, fallback_id)
    local wanted = requested_id or fallback_id
    local fallback = nil
    for index, entry in ipairs(entries) do
        if entry.id == wanted then return index - 1 end
        if entry.id == fallback_id then fallback = index - 1 end
    end
    if fallback ~= nil then return fallback end
    if #entries > 0 then return 0 end
    error("ColorSpaceTransform enum is empty")
end

local function resolve_color_selections(catalog, saved)
    return {
        sourceColorSpace = index_by_id(catalog.colorSpaces,
            saved.sourceColorSpace, COLOR_DEFAULTS.sourceColorSpace),
        sourceGamma = index_by_id(catalog.gammas,
            saved.sourceGamma, COLOR_DEFAULTS.sourceGamma),
        workingColorSpace = index_by_id(catalog.colorSpaces,
            saved.workingColorSpace, COLOR_DEFAULTS.workingColorSpace),
        workingGamma = index_by_id(catalog.gammas,
            saved.workingGamma, COLOR_DEFAULTS.workingGamma),
    }
end

local function joined_ids(entries)
    local ids = {}
    for _, entry in ipairs(entries) do ids[#ids + 1] = entry.id end
    return table.concat(ids, "\n")
end

local function write_value(tool, name, value, time)
    local input = find_input(tool, name)
    if input == nil then
        error("new ShotConfig is missing control '" .. name .. "'")
    end
    input[time] = value
end

local function flow_position(comp, tool)
    if tool == nil or comp.CurrentFrame == nil then return nil, nil end
    local flow = comp.CurrentFrame.FlowView
    if flow == nil then return nil, nil end
    local ok, x, y = pcall(function() return flow:GetPos(tool) end)
    if ok then return x, y end
    return nil, nil
end

local function set_flow_position(comp, tool, x, y)
    if x == nil or y == nil or comp.CurrentFrame == nil then return end
    local flow = comp.CurrentFrame.FlowView
    if flow ~= nil then pcall(function() flow:SetPos(tool, x, y) end) end
end

local function delete_tool(comp, tool)
    local ok, result = pcall(function() return tool:Delete() end)
    if not ok or result == false then
        error("unable to remove previous ShotConfig")
    end
end

local function fusion_application()
    if rawget(_G, "bmd") ~= nil then
        local ok, application = pcall(function()
            return bmd.scriptapp("Fusion", "localhost")
        end)
        if ok and application ~= nil then return application end
    end
    local candidate = rawget(_G, "fu")
    if candidate ~= nil then
        local ok, new_comp = pcall(function() return candidate.NewComp end)
        if ok and new_comp ~= nil then return candidate end
    end
    return nil
end

function M.run(comp_override)
    local active_comp = comp_override or rawget(_G, "comp")
    if active_comp == nil then error("ShotConfig builder requires an active composition") end

    local matches = role_matches(active_comp)
    if #matches > 1 then
        error("multiple ShotConfig components found; rebuild aborted")
    end
    local previous = matches[1]
    local time = active_comp.CurrentTime or 0
    local saved_values = capture_values(previous, time)
    local saved_color_selection = capture_color_selection(previous, time)
    local x, y = flow_position(active_comp, previous)

    local fusion = fusion_application()
    if fusion == nil then error("Fusion application is unavailable") end
    local catalog = color_catalog.sync(fusion)
    local color_selections = resolve_color_selections(catalog, saved_color_selection)

    active_comp:StartUndo("Build ShotConfig v0.1")
    active_comp:Lock()
    local new_config = nil
    local ok, failure = pcall(function()
        new_config = create_group(active_comp, catalog, color_selections)

        for _, name in ipairs(PRESERVED_CONTROLS) do
            local value = saved_values[name]
            if value == nil then value = DEFAULTS[name] end
            write_value(new_config, name, value, time)
        end
        write_value(new_config, CONTROL.source_color_space_choice,
            color_selections.sourceColorSpace, time)
        write_value(new_config, CONTROL.source_gamma_choice,
            color_selections.sourceGamma, time)
        write_value(new_config, CONTROL.working_color_space_choice,
            color_selections.workingColorSpace, time)
        write_value(new_config, CONTROL.working_gamma_choice,
            color_selections.workingGamma, time)
        write_value(new_config, CONTROL.status,
            previous and "Ready (rebuilt; values preserved)" or "Ready", time)

        if previous ~= nil then delete_tool(active_comp, previous) end
        new_config:SetAttrs({ TOOLS_Name = "G_ShotConfig" })
        new_config:SetData("ShotConfig.Role", apply.ROLE)
        new_config:SetData("ShotConfig.SchemaVersion", apply.SCHEMA_VERSION)
        new_config:SetData("ShotConfig." .. apply.DATA.color_space_ids,
            joined_ids(catalog.colorSpaces))
        new_config:SetData("ShotConfig." .. apply.DATA.gamma_ids,
            joined_ids(catalog.gammas))
        new_config:SetData("ShotConfig.ColorCatalogFusionVersion",
            catalog.fusionVersion)
        local preview = dofile(LIVE_PREVIEW_PATH)
        local refreshed, refresh_error = preview.run(new_config)
        if not refreshed then error(refresh_error) end
        set_flow_position(active_comp, new_config, x, y)
        active_comp:SetActiveTool(new_config)
    end)

    if not ok then
        if new_config ~= nil then pcall(function() new_config:Delete() end) end
        pcall(function() active_comp:Unlock() end)
        active_comp:EndUndo(false)
        error("ShotConfig build failed: " .. tostring(failure))
    end
    active_comp:Unlock()
    active_comp:EndUndo(true)
    log(previous and "Rebuilt ShotConfig; current values preserved" or
        "Created ShotConfig v0.1")
    return new_config
end

if rawget(_G, "comp") ~= nil then
    M.run(comp)
end

return M
