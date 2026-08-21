-- ShotConfig v0.1 acceptance tests against Fusion Standalone 21.
-- The runner closes its private acceptance comp and restores the previous comp.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then source = source:sub(2) end
    return source:match("^(.*[/\\])") or "./"
end

local TEST_DIR = script_directory()
local SHOT_CONFIG_DIR = TEST_DIR .. "../"
local BUILD_PATH = SHOT_CONFIG_DIR .. "build_shot_config.lua"
local APPLY_PATH = SHOT_CONFIG_DIR .. "apply_shot_config.lua"
local REFRESH_PATH = SHOT_CONFIG_DIR .. "refresh_resolved_paths.lua"

local fusion = nil
for _ = 1, 60 do
    fusion = bmd.scriptapp("Fusion", "localhost")
    if fusion ~= nil then break end
    bmd.wait(1)
end
if fusion == nil then error("Fusion Standalone is not reachable") end

local temp_comp = dofile(TEST_DIR .. "../../test_support/temp_comp.lua")
local comp = temp_comp.acquire(fusion)
if comp == nil then error("Fusion could not create an isolated test composition") end
local previous_global_comp = rawget(_G, "comp")
_G.comp = comp
print("FUSION_TEMP_COMP_CREATED")

local function fail(message)
    error("ShotConfig acceptance failure: " .. message, 2)
end

local function assert_equal(actual, expected, label)
    if actual ~= expected then
        fail(string.format("%s: expected %q, got %q", label,
            tostring(expected), tostring(actual)))
    end
end

local function assert_true(value, label)
    if not value then fail(label) end
end

local function add_tool(reg_id, name, x, y)
    local tool = comp:AddTool(reg_id, x, y)
    if tool == nil then fail("unable to add " .. reg_id) end
    tool:SetAttrs({ TOOLS_Name = name })
    return tool
end

local function input(tool, name)
    local result = tool[name]
    if result == nil then fail(name .. " input is missing") end
    return result
end

local function set(tool, name, value)
    input(tool, name)[comp.CurrentTime] = value
end

local function get(tool, name)
    return input(tool, name)[comp.CurrentTime]
end

local function data(tool, key)
    return tool:GetData("ShotConfig." .. key)
end

local function set_target(config, apply, kind, index, node_name, template)
    local node_control, template_control = apply.target_controls(kind, index)
    set(config, node_control, node_name or "")
    set(config, template_control, template or "")
end

local function clear_targets(config, apply, kind, first_index)
    for index = first_index or 1, apply.TARGET_SLOT_COUNT do
        set_target(config, apply, kind, index, "", "")
    end
end

local function index_for_id(serialized, wanted)
    local index = 0
    local normalized = tostring(serialized):gsub("\r\n", "\n"):gsub("\r", "\n")
    for line in (normalized .. "\n"):gmatch("(.-)\n") do
        if line ~= "" then
            if line == wanted then return index end
            index = index + 1
        end
    end
    fail("enum ID not embedded in ShotConfig: " .. wanted)
end

local ok, failure = pcall(function()
    comp:Lock()
    local path_map_probe = add_tool("Loader", "PathMapProbe", -1, 0)
    local main_plate = add_tool("Loader", "MainPlate", 0, 0)
    local phone_ui = add_tool("Loader", "PhoneUI", 1, 0)
    local ignored_loader = add_tool("Loader", "IgnoredLoader", 2, 0)
    local master_out = add_tool("Saver", "MasterOut", 0, 2)
    local client_review = add_tool("Saver", "ClientReview", 1, 2)
    local ignored_saver = add_tool("Saver", "IgnoredSaver", 2, 2)
    set(ignored_loader, "Clip", "_KEEP:/ignored_loader.mov")
    set(ignored_saver, "Clip", "_KEEP:/ignored_saver.mov")
    comp:Unlock()

    dofile(BUILD_PATH)
    local apply = dofile(APPLY_PATH)
    local refresh = dofile(REFRESH_PATH)
    local config, find_error = apply.find_config(comp)
    assert_true(config ~= nil, find_error or "builder did not create ShotConfig")
    assert_equal(config:GetData("ShotConfig.Role"), "ShotConfig", "stable role")
    assert_equal(tonumber(config:GetData("ShotConfig.SchemaVersion")), 1,
        "schema version")

    local initial_values, color_error = apply.read_config(config, comp.CurrentTime)
    assert_true(initial_values ~= nil, color_error or "color config unreadable")
    assert_equal(initial_values.sourceColorSpace, "REC709_COLORSPACE",
        "default source color space ID")
    assert_equal(initial_values.sourceGamma, "TWOPOINTFOUR_GAMMA",
        "default source gamma ID")
    assert_equal(initial_values.workingColorSpace, "REC709_COLORSPACE",
        "default working color space ID")
    assert_equal(initial_values.workingGamma, "LINEAR_GAMMA",
        "default working gamma ID")

    local default_loader_node, default_loader_template, default_loader_resolved =
        apply.target_controls("Loader", apply.TARGET_SLOT_COUNT)
    assert_equal(get(config, default_loader_node), "",
        "default Loader slot is inactive")
    assert_equal(get(config, default_loader_template),
        "{root}/{show}_{episode}/BRUTOS/{show}_{episode}_{shot}.mov",
        "default Loader template")
    assert_equal(get(config, default_loader_resolved),
        "_SHOW:/SHOW_E01/BRUTOS/SHOW_E01_0010.mov",
        "live default Loader preview")
    local default_saver_node, default_saver_template, default_saver_resolved =
        apply.target_controls("Saver", apply.TARGET_SLOT_COUNT)
    assert_equal(get(config, default_saver_node), "",
        "default Saver slot is inactive")
    assert_equal(get(config, default_saver_template),
        "{root}/{show}_{episode}/RENDERS/{show}_{episode}_{shot}_{version}.mov",
        "default Saver template")
    assert_equal(get(config, default_saver_resolved),
        "_SHOW:/SHOW_E01/RENDERS/SHOW_E01_0010_v001.mov",
        "live default Saver preview")
    local saver_seed_names = { "Comp", "WIP", "GFX", "Final" }
    for index, expected_name in ipairs(saver_seed_names) do
        local node_control = apply.target_controls("Saver", index)
        assert_equal(get(config, node_control), expected_name,
            "default Saver seed " .. expected_name)
    end

    assert_true(config.SC_ColorSpaceIds == nil,
        "internal color-space control must not be visible")
    assert_true(config.SC_GammaIds == nil,
        "internal gamma control must not be visible")
    assert_equal(tonumber(get(config, "SC_LoaderGroup")), 0,
        "Loader targets rollout default")
    assert_equal(tonumber(get(config, "SC_SaverGroup")), 0,
        "Saver targets rollout default")

    local _, optional_template, optional_resolved =
        apply.target_controls("Loader", 3)
    set(config, optional_template,
        "{workingWidth}x{workingHeight}|{cropX}|{cropY}|" ..
        "{reviewWidth}x{reviewHeight}|{sourceColorSpace}|{sourceGamma}|" ..
        "{workingColorSpace}|{workingGamma}|{embeddedAlpha}")
    assert_true(refresh.run(config), "refreshes after a template change")
    assert_equal(get(config, optional_resolved),
        "3840x2160|2|1|1920x1080|REC709_COLORSPACE|TWOPOINTFOUR_GAMMA|" ..
        "REC709_COLORSPACE|LINEAR_GAMMA|false",
        "live preview resolves the full token contract")

    set(config, apply.CONTROL.source_color_space_choice,
        index_for_id(data(config, apply.DATA.color_space_ids), "DWG_COLORSPACE"))
    set(config, apply.CONTROL.source_gamma_choice,
        index_for_id(data(config, apply.DATA.gamma_ids), "DAV_INTER_OETF_GAMMA"))

    -- Path Map test: write the exact portable contract once, then leave this
    -- Loader untouched. The remaining cases use an intentionally unresolved
    -- test Path Map so Fusion never opens real media or asks about trimming.
    set_target(config, apply, "Loader", 1, "PathMapProbe",
        "{root}/{show}_{episode}/BRUTOS/{show}_{episode}_{shot}.mov")
    clear_targets(config, apply, "Loader", 2)
    clear_targets(config, apply, "Saver")
    local applied, apply_error = apply.run(comp)
    assert_true(applied, apply_error or "Path Map Apply failed")
    assert_equal(get(path_map_probe, "Clip"),
        "_SHOW:/SHOW_E01/BRUTOS/SHOW_E01_0010.mov", "portable Path Map")
    local _, _, loader_one_resolved = apply.target_controls("Loader", 1)
    assert_equal(get(config, loader_one_resolved),
        "_SHOW:/SHOW_E01/BRUTOS/SHOW_E01_0010.mov",
        "portable live resolved-path preview")

    set(config, apply.CONTROL.root, "_SHOTCONFIG_TEST:")
    set_target(config, apply, "Loader", 1, "MainPlate",
        "{root}/{show}_{episode}/BRUTOS/{show}_{episode}_{shot}.mov")
    set_target(config, apply, "Loader", 2, "PhoneUI",
        "{root}/{show}_{episode}/SCREENS/{shot}.mov")
    clear_targets(config, apply, "Loader", 3)
    set_target(config, apply, "Saver", 1, "MasterOut",
        "{root}/{show}_{episode}/RENDERS/{show}_{episode}_{shot}_{version}.mov")
    set_target(config, apply, "Saver", 2, "ClientReview",
        "{root}/{show}_{episode}/WIP/{show}_{episode}_{shot}_REF_{version}.mov")
    clear_targets(config, apply, "Saver", 3)

    -- Establish the isolated starting values for the remaining cases.
    applied, apply_error = apply.run(comp)
    assert_true(applied, apply_error or "initial Apply failed")

    -- Test A: shot change affects only declared targets.
    set(config, apply.CONTROL.shot, "0020")
    assert_true(refresh.run(config), "refreshes after a shot change")
    assert_equal(get(config, loader_one_resolved),
        "_SHOTCONFIG_TEST:/SHOW_E01/BRUTOS/SHOW_E01_0020.mov",
        "preview updates before Apply")
    applied, apply_error = apply.run(comp)
    assert_true(applied, apply_error or "shot change Apply failed")
    assert_equal(get(main_plate, "Clip"),
        "_SHOTCONFIG_TEST:/SHOW_E01/BRUTOS/SHOW_E01_0020.mov", "Test A MainPlate")
    assert_equal(get(phone_ui, "Clip"),
        "_SHOTCONFIG_TEST:/SHOW_E01/SCREENS/0020.mov", "Test A PhoneUI")
    assert_equal(get(ignored_loader, "Clip"), "_KEEP:/ignored_loader.mov",
        "Test A unregistered Loader")
    assert_equal(get(ignored_saver, "Clip"), "_KEEP:/ignored_saver.mov",
        "Test A unregistered Saver")

    -- Test B: only templates containing version change.
    local plate_before_version = get(main_plate, "Clip")
    local phone_before_version = get(phone_ui, "Clip")
    set(config, apply.CONTROL.version, "v002")
    applied, apply_error = apply.run(comp)
    assert_true(applied, apply_error or "version change Apply failed")
    assert_equal(get(main_plate, "Clip"), plate_before_version,
        "Test B version-free MainPlate")
    assert_equal(get(phone_ui, "Clip"), phone_before_version,
        "Test B version-free PhoneUI")
    assert_equal(get(master_out, "Clip"),
        "_SHOTCONFIG_TEST:/SHOW_E01/RENDERS/SHOW_E01_0020_v002.mov", "Test B MasterOut")
    assert_equal(get(client_review, "Clip"),
        "_SHOTCONFIG_TEST:/SHOW_E01/WIP/SHOW_E01_0020_REF_v002.mov",
        "Test B ClientReview")

    -- Test C: one nonexistent target aborts before all writes.
    local plate_before_error = get(main_plate, "Clip")
    local master_before_error = get(master_out, "Clip")
    set(config, apply.CONTROL.shot, "0030")
    set_target(config, apply, "Loader", 1, "MainPlate",
        "{root}/would-change-{shot}.mov")
    set_target(config, apply, "Loader", 2, "MissingPlate",
        "{root}/missing.mov")
    clear_targets(config, apply, "Loader", 3)
    applied = apply.run(comp)
    assert_true(not applied, "Test C invalid target should fail")
    assert_equal(get(main_plate, "Clip"), plate_before_error,
        "Test C Loader transaction")
    assert_equal(get(master_out, "Clip"), master_before_error,
        "Test C Saver transaction")

    -- Test D was exercised with arbitrary node names throughout. Restore the
    -- registry and prove no script change was required.
    set_target(config, apply, "Loader", 1, "MainPlate",
        "{root}/PLATES/{episode}/{shot}.exr")
    set_target(config, apply, "Loader", 2, "PhoneUI",
        "{root}/SCREENS/{shot}.mov")
    clear_targets(config, apply, "Loader", 3)
    set(config, apply.CONTROL.shot, "0040")
    applied, apply_error = apply.run(comp)
    assert_true(applied, apply_error or "Test D arbitrary naming failed")
    assert_equal(get(main_plate, "Clip"), "_SHOTCONFIG_TEST:/PLATES/E01/0040.exr",
        "Test D MainPlate")

    -- Unknown tokens also abort before writes.
    local before_unknown = get(main_plate, "Clip")
    set_target(config, apply, "Loader", 1, "MainPlate",
        "{root}/{notAContractToken}.mov")
    clear_targets(config, apply, "Loader", 2)
    applied = apply.run(comp)
    assert_true(not applied, "unknown token should fail")
    assert_equal(get(main_plate, "Clip"), before_unknown,
        "unknown token transaction")

    -- A partially filled slot is invalid and cannot produce partial writes.
    set_target(config, apply, "Loader", 1, "MainPlate",
        "{root}/would-change-incomplete-slot.mov")
    set_target(config, apply, "Loader", 2, "PhoneUI", "")
    clear_targets(config, apply, "Loader", 3)
    applied = apply.run(comp)
    assert_true(not applied, "partially filled target slot should fail")
    assert_equal(get(main_plate, "Clip"), before_unknown,
        "partial slot transaction")

    -- Test E: rebuild preserves current values and creates one instance.
    set_target(config, apply, "Loader", 1, "MainPlate",
        "{root}/PLATES/{episode}/{shot}.exr")
    clear_targets(config, apply, "Loader", 2)
    set(config, apply.CONTROL.show, "CUSTOM")
    set(config, apply.CONTROL.working_resolution, { 4096, 1716 })
    dofile(BUILD_PATH)
    local rebuilt, rebuild_error = apply.find_config(comp)
    assert_true(rebuilt ~= nil, rebuild_error or "Test E rebuild missing")
    assert_equal(get(rebuilt, apply.CONTROL.show), "CUSTOM", "Test E identity")
    local resolution = get(rebuilt, apply.CONTROL.working_resolution)
    assert_equal(tonumber(resolution[1]), 4096, "Test E working width")
    assert_equal(tonumber(resolution[2]), 1716, "Test E working height")
    local rebuilt_values, rebuilt_color_error =
        apply.read_config(rebuilt, comp.CurrentTime)
    assert_true(rebuilt_values ~= nil,
        rebuilt_color_error or "Test E rebuilt color config unreadable")
    assert_equal(rebuilt_values.sourceColorSpace, "DWG_COLORSPACE",
        "Test E source color selection preserved by ID")
    assert_equal(rebuilt_values.sourceGamma, "DAV_INTER_OETF_GAMMA",
        "Test E source gamma selection preserved by ID")
    local role_count = 0
    for _, tool in pairs(comp:GetToolList(false) or {}) do
        if tool:GetData("ShotConfig.Role") == "ShotConfig" then
            role_count = role_count + 1
        end
    end
    assert_equal(role_count, 1, "Test E unique ShotConfig")

    -- Malformed editable JSON regenerates from the seed and live CST enums.
    local temporary_catalog = os.tmpname() .. ".json"
    local malformed = assert(io.open(temporary_catalog, "wb"))
    malformed:write("{ this is not valid JSON")
    malformed:close()
    local color_catalog = dofile(SHOT_CONFIG_DIR .. "color_enum_catalog.lua")
    local regenerated = color_catalog.sync(fusion, temporary_catalog)
    assert_equal(#regenerated.colorSpaces, 45,
        "malformed JSON regenerated color spaces")
    assert_equal(#regenerated.gammas, 62,
        "malformed JSON regenerated gammas")
    assert_equal(regenerated.colorSpaces[1].label, "Rec.709",
        "malformed JSON regenerated curated seed")
    os.remove(temporary_catalog)

    local reconciled = color_catalog.reconcile({
        colorSpaces = { "KEEP_COLORSPACE", "NEW_COLORSPACE" },
        gammas = { "KEEP_GAMMA", "NEW_GAMMA" },
    }, {
        colorSpaces = {
            { id = "KEEP_COLORSPACE", label = "Friendly Keep" },
            { id = "RETIRED_COLORSPACE", label = "Retired" },
        },
        gammas = {
            { id = "KEEP_GAMMA", label = "Friendly Gamma" },
            { id = "RETIRED_GAMMA", label = "Retired" },
        },
    }, { colorSpaces = {}, gammas = {} }, "test")
    assert_equal(#reconciled.colorSpaces, 2, "retired color ID removed")
    assert_equal(reconciled.colorSpaces[1].label, "Friendly Keep",
        "edited color label preserved")
    assert_equal(reconciled.colorSpaces[2].label, "NEW_COLORSPACE",
        "new color ID uses literal label")
    assert_equal(#reconciled.gammas, 2, "retired gamma ID removed")
    assert_equal(reconciled.gammas[2].label, "NEW_GAMMA",
        "new gamma ID uses literal label")

    print("SHOTCONFIG_FUSION_TESTS_OK")
end)

_G.comp = previous_global_comp
if not ok then
    error(failure)
end
