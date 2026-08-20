-- Runtime validation for the currently open InputPrep prototype comp.

local fusion = bmd.scriptapp("Fusion", "localhost")
if fusion == nil then error("Fusion Standalone is not reachable") end
local comp = fusion.CurrentComp
if comp == nil then error("InputPrep prototype comp is not open") end

local function assert_true(value, message)
    if not value then error("InputPrep prototype failure: " .. message) end
end

local input_prep = nil
for _, tool in pairs(comp:GetToolList(false) or {}) do
    if tool:GetData("InputPrep.Role") == "InputPrep" then
        assert_true(input_prep == nil, "multiple InputPrep prototypes found")
        input_prep = tool
    end
end
assert_true(input_prep ~= nil, "InputPrep prototype not found")
assert_true(tonumber(input_prep:GetData("InputPrep.SchemaVersion")) == 1,
    "unexpected schema")

local source = comp:FindTool("PrototypeSource_3840x2160")
assert_true(source ~= nil, "prototype source is missing")
assert_true(tostring(source.Type[comp.CurrentTime]) == "Corner",
    "prototype source is not using the corner test pattern")

local main_input = input_prep.MainInput1
assert_true(main_input ~= nil, "MainInput1 is missing")
local input_attrs = main_input:GetAttrs() or {}
assert_true(input_attrs.INPB_Connected == true, "MainInput1 is not connected")

local function assert_toggle(id, expected)
    local input = input_prep[id]
    assert_true(input ~= nil, id .. " is missing")
    assert_true(tonumber(input[comp.CurrentTime]) == expected,
        id .. " has an unexpected default")
    input[comp.CurrentTime] = expected == 1 and 0 or 1
    assert_true(tonumber(input[comp.CurrentTime]) ~= expected,
        id .. " is not editable")
    input[comp.CurrentTime] = expected
end

assert_toggle("IP_EnableDepth", 1)
assert_true(tonumber(input_prep.IP_Depth[comp.CurrentTime]) == 3,
    "IP_Depth has an unexpected default")
assert_toggle("IP_EnableColor", 1)
assert_toggle("IP_EnableResize", 1)
assert_toggle("IP_EnableCrop", 1)
assert_toggle("IP_EmbeddedAlpha", 0)

local output = nil
for _, candidate in pairs(input_prep:GetOutputList() or {}) do
    output = candidate
    break
end
assert_true(output ~= nil, "main output is missing")

local status = tostring(input_prep.IP_Status[comp.CurrentTime] or "")
assert_true(status:find("3840 x 2160", 1, true) ~= nil,
    "working raster is missing from status")
assert_true(status:find("3840 x 1920", 1, true) ~= nil,
    "crop raster is missing from status")
assert_true(status:find("TWOPOINTFOUR_GAMMA", 1, true) ~= nil,
    "source gamma is missing from status")
assert_true(status:find("LINEAR_GAMMA", 1, true) ~= nil,
    "working gamma is missing from status")
assert_true(status:find("Depth: 3", 1, true) ~= nil,
    "output depth is missing from status")
print("INPUTPREP_PROTOTYPE_TEST_OK")
comp:SetActiveTool(input_prep)
