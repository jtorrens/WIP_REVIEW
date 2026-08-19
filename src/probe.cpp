#include "probe_core.hpp"
#include "text_rasterizer.hpp"
#include "token_resolver.hpp"

#include <ofxColour.h>
#include <ofx-native-v1.5_aces-v1.3_ocio-v2.3.h>
#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMessage.h>
#include <ofxParam.h>
#include <ofxProperty.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
#define WIPREVIEW_EXPORT __attribute__((visibility("default")))
#elif defined(_WIN32)
#define WIPREVIEW_EXPORT __declspec(dllexport)
#else
#error Unsupported platform
#endif

#ifndef WIPREVIEW_PROBE_VERSION
#define WIPREVIEW_PROBE_VERSION "dev"
#endif

namespace {

constexpr char kPluginIdentifier[] = "com.jtorrens.WIPReviewProbe";
constexpr char kFilterPluginIdentifier[] = "com.jtorrens.WIPReviewProbe.Filter";
constexpr char kParamRequestRod[] = "requestCustomRoD";
constexpr char kParamWidth[] = "requestedWidth";
constexpr char kParamHeight[] = "requestedHeight";
constexpr char kParamScenario[] = "scenarioLabel";
constexpr char kParamAnimatedString[] = "animatedStringProbe";
constexpr char kParamCanvasMode[] = "canvasMode";
constexpr char kParamPlacement[] = "placementMode";
constexpr char kParamResample[] = "resampleFilter";
constexpr char kParamCanvasColour[] = "canvasColour";
constexpr char kParamColorGroup[] = "managedColorGroup";
constexpr char kParamColorSpaceMode[] = "colorSpaceMode";
constexpr char kParamManualColorSpace[] = "manualColorSpace";
constexpr char kParamGraphicsWhiteMode[] = "graphicsWhiteMode";
constexpr char kParamGraphicsWhiteNits[] = "graphicsWhiteNits";
constexpr char kParamHlgPeakNits[] = "hlgPeakNits";
constexpr char kParamBlankingEnabled[] = "blankingEnabled";
constexpr char kParamBlankingAspectPreset[] = "blankingAspectPreset";
constexpr char kParamBlankingAspectCustom[] = "blankingAspectCustom";
constexpr char kParamBlankingColour[] = "blankingColor";
constexpr char kParamBlankingOpacity[] = "blankingOpacity";
constexpr char kParamFontFamily[] = "fontFamily";
constexpr char kParamFontStyle[] = "fontStyle";
constexpr char kParamFontSize[] = "fontSize";
constexpr char kParamTextColour[] = "textColor";
constexpr char kParamTextOpacity[] = "textOpacity";
constexpr char kParamTextGroup[] = "textGlobalGroup";
constexpr char kParamZoneGap[] = "zoneGap";
constexpr char kParamOverflowMode[] = "overflowMode";
constexpr char kParamMinimumFontScale[] = "minimumFontScale";
constexpr char kParamDynamicTextGroup[] = "dynamicTextGroup";
constexpr char kParamFrameRelativeBase[] = "frameRelativeBase";
constexpr char kParamFrameStart[] = "frameStart";
constexpr char kParamFpsMode[] = "fpsMode";
constexpr char kParamFpsOverride[] = "fpsOverride";
constexpr char kParamTimecodeStart[] = "timecodeStart";
constexpr char kParamDropFrameMode[] = "dropFrameMode";
constexpr char kParamOutlineEnabled[] = "outlineEnabled";
constexpr char kParamOutlineWidth[] = "outlineWidth";
constexpr char kParamOutlineColour[] = "outlineColor";
constexpr char kParamOutlineOpacity[] = "outlineOpacity";
constexpr char kParamOutlineGroup[] = "outlineGroup";
constexpr char kParamShadowEnabled[] = "shadowEnabled";
constexpr char kParamShadowOffsetX[] = "shadowOffsetX";
constexpr char kParamShadowOffsetY[] = "shadowOffsetY";
constexpr char kParamShadowSoftness[] = "shadowSoftness";
constexpr char kParamShadowColour[] = "shadowColor";
constexpr char kParamShadowOpacity[] = "shadowOpacity";
constexpr char kParamShadowGroup[] = "shadowGroup";
constexpr char kParamPaddingLeft[] = "paddingLeft";
constexpr char kParamPaddingRight[] = "paddingRight";
constexpr char kParamPaddingTop[] = "paddingTop";
constexpr char kParamPaddingBottom[] = "paddingBottom";

struct ZoneParamNames {
  const char* label;
  const char* group;
  const char* enabled;
  const char* text;
  const char* useSize;
  const char* size;
  const char* useColour;
  const char* colour;
  const char* useOpacity;
  const char* opacity;
  const char* offsetX;
  const char* offsetY;
};

constexpr std::array<ZoneParamNames, 6> kZoneParams{{
    {"TL", "tlZone", "tlEnabled", "tlText", "tlUseSizeOverride", "tlSize",
     "tlUseColorOverride", "tlColor", "tlUseOpacityOverride", "tlOpacity",
     "tlOffsetX", "tlOffsetY"},
    {"TC", "tcZone", "tcEnabled", "tcText", "tcUseSizeOverride", "tcSize",
     "tcUseColorOverride", "tcColor", "tcUseOpacityOverride", "tcOpacity",
     "tcOffsetX", "tcOffsetY"},
    {"TR", "trZone", "trEnabled", "trText", "trUseSizeOverride", "trSize",
     "trUseColorOverride", "trColor", "trUseOpacityOverride", "trOpacity",
     "trOffsetX", "trOffsetY"},
    {"BL", "blZone", "blEnabled", "blText", "blUseSizeOverride", "blSize",
     "blUseColorOverride", "blColor", "blUseOpacityOverride", "blOpacity",
     "blOffsetX", "blOffsetY"},
    {"BC", "bcZone", "bcEnabled", "bcText", "bcUseSizeOverride", "bcSize",
     "bcUseColorOverride", "bcColor", "bcUseOpacityOverride", "bcOpacity",
     "bcOffsetX", "bcOffsetY"},
    {"BR", "brZone", "brEnabled", "brText", "brUseSizeOverride", "brSize",
     "brUseColorOverride", "brColor", "brUseOpacityOverride", "brOpacity",
     "brOffsetX", "brOffsetY"},
}};
constexpr char kNativeConfig[] = "ofx-native-v1.5_aces-v1.3_ocio-v2.3";
constexpr char kOutputClipPARPreference[] = "OfxImageClipPropPAR_Output";
constexpr char kSourcePreferredColourspaces[] =
    "OfxImageClipPropPreferredColourspaces_Source";

OfxHost* gHost = nullptr;
const OfxImageEffectSuiteV1* gImageSuite = nullptr;
const OfxPropertySuiteV1* gPropertySuite = nullptr;
const OfxParameterSuiteV1* gParameterSuite = nullptr;
const OfxMessageSuiteV2* gMessageSuite = nullptr;

const char* statusName(OfxStatus status) noexcept {
  switch (status) {
    case kOfxStatOK: return "OK";
    case kOfxStatFailed: return "Failed";
    case kOfxStatErrFatal: return "ErrFatal";
    case kOfxStatErrUnknown: return "ErrUnknown";
    case kOfxStatErrMissingHostFeature: return "ErrMissingHostFeature";
    case kOfxStatErrUnsupported: return "ErrUnsupported";
    case kOfxStatErrExists: return "ErrExists";
    case kOfxStatErrFormat: return "ErrFormat";
    case kOfxStatErrMemory: return "ErrMemory";
    case kOfxStatErrBadHandle: return "ErrBadHandle";
    case kOfxStatErrBadIndex: return "ErrBadIndex";
    case kOfxStatErrValue: return "ErrValue";
    case kOfxStatReplyYes: return "ReplyYes";
    case kOfxStatReplyNo: return "ReplyNo";
    case kOfxStatReplyDefault: return "ReplyDefault";
    default: return "UnknownStatus";
  }
}

class Logger {
 public:
  static Logger& instance() {
    static Logger logger;
    return logger;
  }

  void write(const std::string& event, const std::string& fields = {}) noexcept {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      ensureOpen();
      if (!stream_) {
        return;
      }
      stream_ << timestamp() << " | pid=" << processId_ << " | " << event;
      if (!fields.empty()) {
        stream_ << " | " << fields;
      }
      stream_ << '\n';
      stream_.flush();
    } catch (...) {
      // Diagnostics must never destabilise the host.
    }
  }

  const std::string& path() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureOpen();
    return path_;
  }

 private:
  Logger() {
#if defined(_WIN32)
    processId_ = 0;
#else
    processId_ = static_cast<long>(::getpid());
#endif
  }

  static std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream value;
    value << std::put_time(&local, "%Y-%m-%dT%H:%M:%S") << '.'
          << std::setfill('0') << std::setw(3) << millis.count();
    return value.str();
  }

  void ensureOpen() {
    if (attempted_) {
      return;
    }
    attempted_ = true;

    if (const char* overridePath = std::getenv("WIPREVIEW_PROBE_LOG");
        overridePath && *overridePath) {
      path_ = overridePath;
    } else if (const char* userDirectory = std::getenv("HOME");
               userDirectory && *userDirectory) {
      path_ = (std::filesystem::path(userDirectory) / "Library" / "Logs" /
               "WIPReviewProbe" / "WIPReviewProbe.log").string();
    } else {
      path_ = "/tmp/WIPReviewProbe.log";
    }

    const std::filesystem::path logPath(path_);
    if (logPath.has_parent_path()) {
      std::error_code error;
      std::filesystem::create_directories(logPath.parent_path(), error);
    }
    stream_.open(path_, std::ios::out | std::ios::app);
  }

  std::mutex mutex_;
  std::ofstream stream_;
  std::string path_;
  bool attempted_ = false;
  long processId_ = 0;
};

std::string quoted(const char* value) {
  return value ? '"' + std::string(value) + '"' : "<null>";
}

std::string joinInts(OfxPropertySetHandle properties, const char* name) {
  if (!gPropertySuite || !properties) return "<no-property-suite>";
  int dimension = 0;
  const OfxStatus dimStatus = gPropertySuite->propGetDimension(properties, name, &dimension);
  if (dimStatus != kOfxStatOK) return std::string("<") + statusName(dimStatus) + ">";
  std::ostringstream output;
  output << '[';
  for (int index = 0; index < dimension; ++index) {
    int value = 0;
    const OfxStatus status = gPropertySuite->propGetInt(properties, name, index, &value);
    if (index) output << ',';
    if (status == kOfxStatOK) output << value;
    else output << '<' << statusName(status) << '>';
  }
  return output.str() + ']';
}

std::string joinDoubles(OfxPropertySetHandle properties, const char* name) {
  if (!gPropertySuite || !properties) return "<no-property-suite>";
  int dimension = 0;
  const OfxStatus dimStatus = gPropertySuite->propGetDimension(properties, name, &dimension);
  if (dimStatus != kOfxStatOK) return std::string("<") + statusName(dimStatus) + ">";
  std::ostringstream output;
  output << '[' << std::setprecision(12);
  for (int index = 0; index < dimension; ++index) {
    double value = 0.0;
    const OfxStatus status = gPropertySuite->propGetDouble(properties, name, index, &value);
    if (index) output << ',';
    if (status == kOfxStatOK) output << value;
    else output << '<' << statusName(status) << '>';
  }
  return output.str() + ']';
}

std::string joinStrings(OfxPropertySetHandle properties, const char* name) {
  if (!gPropertySuite || !properties) return "<no-property-suite>";
  int dimension = 0;
  const OfxStatus dimStatus = gPropertySuite->propGetDimension(properties, name, &dimension);
  if (dimStatus != kOfxStatOK) return std::string("<") + statusName(dimStatus) + ">";
  std::ostringstream output;
  output << '[';
  for (int index = 0; index < dimension; ++index) {
    char* value = nullptr;
    const OfxStatus status = gPropertySuite->propGetString(properties, name, index, &value);
    if (index) output << ',';
    if (status == kOfxStatOK) output << quoted(value);
    else output << '<' << statusName(status) << '>';
  }
  return output.str() + ']';
}

std::string getString(OfxPropertySetHandle properties, const char* name) {
  if (!gPropertySuite || !properties) return "<unavailable>";
  char* value = nullptr;
  const OfxStatus status = gPropertySuite->propGetString(properties, name, 0, &value);
  return status == kOfxStatOK ? quoted(value)
                              : std::string("<") + statusName(status) + ">";
}

std::string getInt(OfxPropertySetHandle properties, const char* name) {
  if (!gPropertySuite || !properties) return "<unavailable>";
  int value = 0;
  const OfxStatus status = gPropertySuite->propGetInt(properties, name, 0, &value);
  return status == kOfxStatOK ? std::to_string(value)
                              : std::string("<") + statusName(status) + ">";
}

std::string getDouble(OfxPropertySetHandle properties, const char* name) {
  if (!gPropertySuite || !properties) return "<unavailable>";
  double value = 0.0;
  const OfxStatus status = gPropertySuite->propGetDouble(properties, name, 0, &value);
  if (status != kOfxStatOK) return std::string("<") + statusName(status) + ">";
  std::ostringstream output;
  output << std::setprecision(12) << value;
  return output.str();
}

void logHostCapabilities() {
  if (!gHost || !gHost->host) return;
  OfxPropertySetHandle host = gHost->host;
  Logger::instance().write("HOST_IDENTITY",
      "name=" + getString(host, kOfxPropName) +
      " label=" + getString(host, kOfxPropLabel) +
      " version=" + joinInts(host, kOfxPropVersion) +
      " version_label=" + getString(host, kOfxPropVersionLabel) +
      " api_version=" + joinInts(host, kOfxPropAPIVersion));
  Logger::instance().write("HOST_CONTEXTS",
      "supported_contexts=" + joinStrings(host, kOfxImageEffectPropSupportedContexts));
  Logger::instance().write("HOST_SPATIAL_CAPABILITIES",
      "multi_resolution=" + getInt(host, kOfxImageEffectPropSupportsMultiResolution) +
      " tiles=" + getInt(host, kOfxImageEffectPropSupportsTiles) +
      " multiple_clip_depths=" + getInt(host, kOfxImageEffectPropSupportsMultipleClipDepths) +
      " multiple_clip_PARs=" + getInt(host, kOfxImageEffectPropSupportsMultipleClipPARs));
  Logger::instance().write("HOST_FORMAT_CAPABILITIES",
      "pixel_depths=" + joinStrings(host, kOfxImageEffectPropSupportedPixelDepths) +
      " components=" + joinStrings(host, kOfxImageEffectPropSupportedComponents));
  Logger::instance().write("HOST_PARAMETER_CAPABILITIES",
      "string_animation=" + getInt(host, kOfxParamHostPropSupportsStringAnimation) +
      " choice_animation=" + getInt(host, kOfxParamHostPropSupportsChoiceAnimation) +
      " boolean_animation=" + getInt(host, kOfxParamHostPropSupportsBooleanAnimation));
  Logger::instance().write("HOST_COLOUR_CAPABILITIES",
      "style=" + getString(host, kOfxImageEffectPropColourManagementStyle) +
      " native_configs=" + joinStrings(host, kOfxImageEffectPropColourManagementAvailableConfigs));
}

struct InstanceData {
  struct ZoneHandles {
    OfxParamHandle enabled = nullptr;
    OfxParamHandle text = nullptr;
    OfxParamHandle useSize = nullptr;
    OfxParamHandle size = nullptr;
    OfxParamHandle useColour = nullptr;
    OfxParamHandle colour = nullptr;
    OfxParamHandle useOpacity = nullptr;
    OfxParamHandle opacity = nullptr;
    OfxParamHandle offsetX = nullptr;
    OfxParamHandle offsetY = nullptr;
  };

  OfxImageEffectHandle effect = nullptr;
  OfxImageClipHandle source = nullptr;
  OfxImageClipHandle output = nullptr;
  OfxParamHandle requestRod = nullptr;
  OfxParamHandle width = nullptr;
  OfxParamHandle height = nullptr;
  OfxParamHandle scenario = nullptr;
  OfxParamHandle animatedString = nullptr;
  OfxParamHandle canvasMode = nullptr;
  OfxParamHandle placement = nullptr;
  OfxParamHandle resample = nullptr;
  OfxParamHandle canvasColour = nullptr;
  OfxParamHandle colorSpaceMode = nullptr;
  OfxParamHandle manualColorSpace = nullptr;
  OfxParamHandle graphicsWhiteMode = nullptr;
  OfxParamHandle graphicsWhiteNits = nullptr;
  OfxParamHandle hlgPeakNits = nullptr;
  OfxParamHandle blankingEnabled = nullptr;
  OfxParamHandle blankingAspectPreset = nullptr;
  OfxParamHandle blankingAspectCustom = nullptr;
  OfxParamHandle blankingColour = nullptr;
  OfxParamHandle blankingOpacity = nullptr;
  OfxParamHandle fontFamily = nullptr;
  OfxParamHandle fontStyle = nullptr;
  OfxParamHandle fontSize = nullptr;
  OfxParamHandle textColour = nullptr;
  OfxParamHandle textOpacity = nullptr;
  OfxParamHandle zoneGap = nullptr;
  OfxParamHandle overflowMode = nullptr;
  OfxParamHandle minimumFontScale = nullptr;
  OfxParamHandle frameRelativeBase = nullptr;
  OfxParamHandle frameStart = nullptr;
  OfxParamHandle fpsMode = nullptr;
  OfxParamHandle fpsOverride = nullptr;
  OfxParamHandle timecodeStart = nullptr;
  OfxParamHandle dropFrameMode = nullptr;
  OfxParamHandle outlineEnabled = nullptr;
  OfxParamHandle outlineWidth = nullptr;
  OfxParamHandle outlineColour = nullptr;
  OfxParamHandle outlineOpacity = nullptr;
  OfxParamHandle shadowEnabled = nullptr;
  OfxParamHandle shadowOffsetX = nullptr;
  OfxParamHandle shadowOffsetY = nullptr;
  OfxParamHandle shadowSoftness = nullptr;
  OfxParamHandle shadowColour = nullptr;
  OfxParamHandle shadowOpacity = nullptr;
  OfxParamHandle paddingLeft = nullptr;
  OfxParamHandle paddingRight = nullptr;
  OfxParamHandle paddingTop = nullptr;
  OfxParamHandle paddingBottom = nullptr;
  std::array<ZoneHandles, 6> zones{};
  std::string context;
  std::uint64_t id = 0;
};

std::atomic<std::uint64_t> gNextInstanceId{1};

InstanceData* getInstance(OfxImageEffectHandle effect) {
  if (!effect || !gImageSuite || !gPropertySuite) return nullptr;
  OfxPropertySetHandle properties = nullptr;
  if (gImageSuite->getPropertySet(effect, &properties) != kOfxStatOK) return nullptr;
  InstanceData* instance = nullptr;
  if (gPropertySuite->propGetPointer(properties, kOfxPropInstanceData, 0,
                                     reinterpret_cast<void**>(&instance)) != kOfxStatOK) {
    return nullptr;
  }
  return instance;
}

std::string instancePrefix(const InstanceData* instance) {
  if (!instance) return "instance=<null>";
  std::ostringstream output;
  output << "instance=" << instance->id << " context=" << quoted(instance->context.c_str());
  return output.str();
}

void logEffectProperties(const InstanceData* instance,
                         OfxPropertySetHandle properties) {
  Logger::instance().write("INSTANCE_PROJECT",
      instancePrefix(instance) +
      " project_size=" + joinDoubles(properties, kOfxImageEffectPropProjectSize) +
      " project_extent=" + joinDoubles(properties, kOfxImageEffectPropProjectExtent) +
      " project_offset=" + joinDoubles(properties, kOfxImageEffectPropProjectOffset) +
      " project_PAR=" + getDouble(properties, kOfxImageEffectPropProjectPixelAspectRatio) +
      " frame_rate=" + getDouble(properties, kOfxImageEffectPropFrameRate));
  Logger::instance().write("INSTANCE_COLOUR_NEGOTIATION",
      instancePrefix(instance) +
      " style=" + getString(properties, kOfxImageEffectPropColourManagementStyle) +
      " native_config=" + getString(properties, kOfxImageEffectPropColourManagementConfig) +
      " ocio_config=" + getString(properties, kOfxImageEffectPropOCIOConfig) +
      " ocio_display=" + getString(properties, kOfxImageEffectPropOCIODisplay) +
      " ocio_view=" + getString(properties, kOfxImageEffectPropOCIOView));
}

void logClip(const InstanceData* instance, const char* label,
             OfxImageClipHandle clip, OfxTime time) {
  if (!clip) {
    Logger::instance().write("CLIP", instancePrefix(instance) + " clip=" + label + " missing=true");
    return;
  }
  OfxPropertySetHandle properties = nullptr;
  const OfxStatus propsStatus = gImageSuite->clipGetPropertySet(clip, &properties);
  OfxRectD rod{};
  const OfxStatus rodStatus = gImageSuite->clipGetRegionOfDefinition(clip, time, &rod);
  std::ostringstream fields;
  fields << instancePrefix(instance) << " clip=" << label
         << " properties_status=" << statusName(propsStatus)
         << " connected=" << getInt(properties, kOfxImageClipPropConnected)
         << " components=" << getString(properties, kOfxImageEffectPropComponents)
         << " depth=" << getString(properties, kOfxImageEffectPropPixelDepth)
         << " PAR=" << getDouble(properties, kOfxImagePropPixelAspectRatio)
         << " frame_rate=" << getDouble(properties, kOfxImageEffectPropFrameRate)
         << " frame_range=" << joinDoubles(properties, kOfxImageEffectPropFrameRange)
         << " unmapped_frame_rate=" << getDouble(properties, kOfxImageEffectPropUnmappedFrameRate)
         << " unmapped_frame_range=" << joinDoubles(properties, kOfxImageEffectPropUnmappedFrameRange)
         << " premultiplication=" << getString(properties, kOfxImageEffectPropPreMultiplication)
         << " colourspace=" << getString(properties, kOfxImageClipPropColourspace)
         << " RoD_status=" << statusName(rodStatus);
  if (rodStatus == kOfxStatOK) {
    fields << " RoD=[" << rod.x1 << ',' << rod.y1 << ',' << rod.x2 << ',' << rod.y2 << ']';
  }
  Logger::instance().write("CLIP", fields.str());
}

void logImage(const InstanceData* instance, const char* label,
              OfxPropertySetHandle image) {
  Logger::instance().write("IMAGE",
      instancePrefix(instance) + " clip=" + label +
      " bounds=" + joinInts(image, kOfxImagePropBounds) +
      " RoD=" + joinInts(image, kOfxImagePropRegionOfDefinition) +
      " render_scale=" + joinDoubles(image, kOfxImageEffectPropRenderScale) +
      " PAR=" + getDouble(image, kOfxImagePropPixelAspectRatio) +
      " components=" + getString(image, kOfxImageEffectPropComponents) +
      " depth=" + getString(image, kOfxImageEffectPropPixelDepth) +
      " row_bytes=" + getInt(image, kOfxImagePropRowBytes) +
      " premultiplication=" + getString(image, kOfxImageEffectPropPreMultiplication) +
      " colourspace=" + getString(image, kOfxImageClipPropColourspace));
}

bool readRodRequest(const InstanceData* instance, bool& enabled, int& width, int& height) {
  if (!instance || !gParameterSuite) return false;
  int request = 1;
  width = 1920;
  height = 1080;
  const OfxStatus a = gParameterSuite->paramGetValue(instance->requestRod, &request);
  const OfxStatus b = gParameterSuite->paramGetValue(instance->width, &width);
  const OfxStatus c = gParameterSuite->paramGetValue(instance->height, &height);
  int canvasMode = 1;
  if (instance->canvasMode) {
    gParameterSuite->paramGetValue(instance->canvasMode, &canvasMode);
  }
  request = request && canvasMode == 1;
  enabled = request != 0;
  width = std::max(1, width);
  height = std::max(1, height);
  return a == kOfxStatOK && b == kOfxStatOK && c == kOfxStatOK;
}

void defineChoiceParam(OfxParamSetHandle params, const char* name, const char* label,
                       const std::vector<const char*>& choices, int defaultValue,
                       const char* hint, const char* parent = nullptr) {
  OfxPropertySetHandle properties = nullptr;
  if (gParameterSuite->paramDefine(params, kOfxParamTypeChoice, name, &properties) != kOfxStatOK) return;
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, label);
  for (std::size_t index = 0; index < choices.size(); ++index) {
    gPropertySuite->propSetString(properties, kOfxParamPropChoiceOption,
                                 static_cast<int>(index), choices[index]);
  }
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, defaultValue);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropHint, 0, hint);
  if (parent) gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, parent);
}

std::size_t pixelBytes(OfxPropertySetHandle image) {
  char* components = nullptr;
  char* depth = nullptr;
  if (gPropertySuite->propGetString(image, kOfxImageEffectPropComponents, 0, &components) != kOfxStatOK ||
      gPropertySuite->propGetString(image, kOfxImageEffectPropPixelDepth, 0, &depth) != kOfxStatOK) {
    return 0;
  }
  std::size_t channels = 0;
  if (std::strcmp(components, kOfxImageComponentRGBA) == 0) channels = 4;
  else if (std::strcmp(components, kOfxImageComponentRGB) == 0) channels = 3;
  else if (std::strcmp(components, kOfxImageComponentAlpha) == 0) channels = 1;

  std::size_t channelBytes = 0;
  if (std::strcmp(depth, kOfxBitDepthByte) == 0) channelBytes = 1;
  else if (std::strcmp(depth, kOfxBitDepthShort) == 0 ||
           std::strcmp(depth, kOfxBitDepthHalf) == 0) channelBytes = 2;
  else if (std::strcmp(depth, kOfxBitDepthFloat) == 0) channelBytes = 4;
  return channels * channelBytes;
}

int channelCount(OfxPropertySetHandle image) {
  char* components = nullptr;
  if (gPropertySuite->propGetString(image, kOfxImageEffectPropComponents, 0, &components) != kOfxStatOK) {
    return 0;
  }
  if (std::strcmp(components, kOfxImageComponentRGBA) == 0) return 4;
  if (std::strcmp(components, kOfxImageComponentRGB) == 0) return 3;
  if (std::strcmp(components, kOfxImageComponentAlpha) == 0) return 1;
  return 0;
}

bool channelType(OfxPropertySetHandle image, wipreview::probe::ChannelType& type) {
  char* depth = nullptr;
  if (gPropertySuite->propGetString(image, kOfxImageEffectPropPixelDepth, 0, &depth) != kOfxStatOK) {
    return false;
  }
  if (std::strcmp(depth, kOfxBitDepthByte) == 0) type = wipreview::probe::ChannelType::UInt8;
  else if (std::strcmp(depth, kOfxBitDepthShort) == 0) type = wipreview::probe::ChannelType::UInt16;
  else if (std::strcmp(depth, kOfxBitDepthHalf) == 0) type = wipreview::probe::ChannelType::Half;
  else if (std::strcmp(depth, kOfxBitDepthFloat) == 0) type = wipreview::probe::ChannelType::Float32;
  else return false;
  return true;
}

wipreview::probe::ImageView imageView(OfxPropertySetHandle image) {
  wipreview::probe::ImageView view;
  void* data = nullptr;
  int bounds[4]{};
  int rowBytes = 0;
  if (gPropertySuite->propGetPointer(image, kOfxImagePropData, 0, &data) == kOfxStatOK &&
      gPropertySuite->propGetIntN(image, kOfxImagePropBounds, 4, bounds) == kOfxStatOK &&
      gPropertySuite->propGetInt(image, kOfxImagePropRowBytes, 0, &rowBytes) == kOfxStatOK) {
    view.data = static_cast<std::byte*>(data);
    view.bounds = {bounds[0], bounds[1], bounds[2], bounds[3]};
    view.rowBytes = rowBytes;
    view.pixelBytes = pixelBytes(image);
    view.channels = channelCount(image);
    if (!channelType(image, view.channelType)) {
      view.pixelBytes = 0;
      view.channels = 0;
    }
  }
  return view;
}

double imagePAR(OfxPropertySetHandle image) {
  double value = 1.0;
  if (!image || gPropertySuite->propGetDouble(
          image, kOfxImagePropPixelAspectRatio, 0, &value) != kOfxStatOK || value <= 0.0) {
    return 1.0;
  }
  return value;
}

bool imageIsPremultiplied(OfxPropertySetHandle image) {
  char* value = nullptr;
  if (!image || gPropertySuite->propGetString(
          image, kOfxImageEffectPropPreMultiplication, 0, &value) != kOfxStatOK || !value) {
    return true;
  }
  return std::strcmp(value, kOfxImageUnPreMultiplied) != 0;
}

const char* displayEncodingName(wipreview::color::DisplayEncoding encoding) noexcept {
  switch (encoding) {
    case wipreview::color::DisplayEncoding::Rec709Gamma24:
      return "Rec.709 Gamma 2.4";
    case wipreview::color::DisplayEncoding::Rec2100PQ:
      return "Rec.2100 PQ";
    case wipreview::color::DisplayEncoding::Rec2100HLG:
      return "Rec.2100 HLG";
  }
  return "Unknown";
}

const char* ofxDisplayColourspace(
    wipreview::color::DisplayEncoding encoding) noexcept {
  switch (encoding) {
    case wipreview::color::DisplayEncoding::Rec709Gamma24:
      return kOfxColourspaceRec1886Rec709Display;
    case wipreview::color::DisplayEncoding::Rec2100PQ:
      return kOfxColourspaceRec2100PqDisplay;
    case wipreview::color::DisplayEncoding::Rec2100HLG:
      return kOfxColourspaceRec2100HlgDisplay;
  }
  return kOfxColourspaceRec1886Rec709Display;
}

bool identifyHostDisplayEncoding(
    const std::string& colourspace,
    wipreview::color::DisplayEncoding& encoding) {
  std::string normalized = colourspace;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  if ((normalized.find("rec2100") != std::string::npos ||
       normalized.find("rec.2100") != std::string::npos) &&
      normalized.find("hlg") != std::string::npos) {
    encoding = wipreview::color::DisplayEncoding::Rec2100HLG;
    return true;
  }
  if (((normalized.find("rec2100") != std::string::npos ||
        normalized.find("rec.2100") != std::string::npos) &&
       normalized.find("pq") != std::string::npos) ||
      normalized.find("st2084") != std::string::npos ||
      normalized.find("st.2084") != std::string::npos) {
    encoding = wipreview::color::DisplayEncoding::Rec2100PQ;
    return true;
  }
  if ((normalized.find("rec709") != std::string::npos ||
       normalized.find("rec.709") != std::string::npos) &&
      (normalized.find("gamma 2.4") != std::string::npos ||
       normalized.find("gamma2.4") != std::string::npos ||
       normalized.find("g24") != std::string::npos ||
       normalized.find("rec1886") != std::string::npos)) {
    encoding = wipreview::color::DisplayEncoding::Rec709Gamma24;
    return true;
  }
  return false;
}

struct ManagedColorSettings {
  wipreview::color::DisplayConfig config;
  std::string hostColourspace;
  int colorSpaceMode = 0;
  int manualColorSpace = 0;
  int graphicsWhiteMode = 0;
  bool hostRecognized = false;
  bool usedManualInterpretation = false;
};

ManagedColorSettings readManagedColorSettings(
    const InstanceData* instance, OfxPropertySetHandle sourceImage) {
  ManagedColorSettings settings;
  double manualWhite = 203.0;
  double hlgPeak = 1000.0;
  if (instance->colorSpaceMode) {
    gParameterSuite->paramGetValue(
        instance->colorSpaceMode, &settings.colorSpaceMode);
  }
  if (instance->manualColorSpace) {
    gParameterSuite->paramGetValue(
        instance->manualColorSpace, &settings.manualColorSpace);
  }
  if (instance->graphicsWhiteMode) {
    gParameterSuite->paramGetValue(
        instance->graphicsWhiteMode, &settings.graphicsWhiteMode);
  }
  if (instance->graphicsWhiteNits) {
    gParameterSuite->paramGetValue(instance->graphicsWhiteNits, &manualWhite);
  }
  if (instance->hlgPeakNits) {
    gParameterSuite->paramGetValue(instance->hlgPeakNits, &hlgPeak);
  }
  settings.colorSpaceMode = std::clamp(settings.colorSpaceMode, 0, 1);
  settings.manualColorSpace = std::clamp(settings.manualColorSpace, 0, 2);
  settings.graphicsWhiteMode = std::clamp(settings.graphicsWhiteMode, 0, 1);
  settings.config.peakNits = std::clamp(hlgPeak, 100.0, 10000.0);

  settings.hostColourspace = getString(sourceImage, kOfxImageClipPropColourspace);
  if (settings.hostColourspace.empty()) {
    OfxPropertySetHandle sourceProperties = nullptr;
    if (gImageSuite->clipGetPropertySet(
            instance->source, &sourceProperties) == kOfxStatOK) {
      settings.hostColourspace = getString(
          sourceProperties, kOfxImageClipPropColourspace);
    }
  }
  settings.hostRecognized = identifyHostDisplayEncoding(
      settings.hostColourspace, settings.config.encoding);
  settings.usedManualInterpretation =
      settings.colorSpaceMode == 1 || !settings.hostRecognized;
  if (settings.usedManualInterpretation) {
    const auto manualEncodings = std::array{
        wipreview::color::DisplayEncoding::Rec709Gamma24,
        wipreview::color::DisplayEncoding::Rec2100PQ,
        wipreview::color::DisplayEncoding::Rec2100HLG};
    settings.config.encoding = manualEncodings[
        static_cast<std::size_t>(settings.manualColorSpace)];
  }
  settings.config.graphicsWhiteNits = settings.graphicsWhiteMode == 0
      ? wipreview::color::automaticGraphicsWhiteNits(
            settings.config.encoding, settings.config.peakNits)
      : std::clamp(manualWhite, 1.0, 10000.0);
  return settings;
}

wipreview::probe::RenderOptions readRenderOptions(const InstanceData* instance,
                                                  OfxTime time,
                                                  OfxPropertySetHandle sourceImage,
                                                  OfxPropertySetHandle outputImage) {
  wipreview::probe::RenderOptions options;
  int placement = 1;
  int filter = 2;
  double canvas[4] = {0.0, 0.0, 0.0, 1.0};
  if (instance->placement) gParameterSuite->paramGetValueAtTime(instance->placement, time, &placement);
  if (instance->resample) gParameterSuite->paramGetValueAtTime(instance->resample, time, &filter);
  if (instance->canvasColour) {
    gParameterSuite->paramGetValueAtTime(instance->canvasColour, time,
                                         &canvas[0], &canvas[1], &canvas[2], &canvas[3]);
  }
  const auto placements = std::array{
      wipreview::probe::PlacementMode::Identity,
      wipreview::probe::PlacementMode::Fit,
      wipreview::probe::PlacementMode::Fill,
      wipreview::probe::PlacementMode::Stretch,
      wipreview::probe::PlacementMode::OneToOne};
  const auto filters = std::array{
      wipreview::probe::ResampleFilter::Bilinear,
      wipreview::probe::ResampleFilter::Bicubic,
      wipreview::probe::ResampleFilter::Lanczos3};
  options.placement = placements[static_cast<std::size_t>(std::clamp(placement, 0, 4))];
  options.filter = filters[static_cast<std::size_t>(std::clamp(filter, 0, 2))];
  options.sourcePixelAspect = imagePAR(sourceImage);
  options.outputPixelAspect = imagePAR(outputImage);
  options.sourcePremultiplied = imageIsPremultiplied(sourceImage);
  options.outputPremultiplied = imageIsPremultiplied(outputImage);
  for (int channel = 0; channel < 4; ++channel) {
    options.canvas[channel] = static_cast<float>(canvas[channel]);
  }
  return options;
}

wipreview::probe::BlankingOptions readBlankingOptions(
    const InstanceData* instance, OfxTime time, OfxPropertySetHandle outputImage) {
  wipreview::probe::BlankingOptions options;
  int enabled = 0;
  int preset = 3;
  double customAspect = 2.0;
  double colour[4] = {0.0, 0.0, 0.0, 1.0};
  double opacity = 1.0;
  if (instance->blankingEnabled) {
    gParameterSuite->paramGetValueAtTime(instance->blankingEnabled, time, &enabled);
  }
  if (instance->blankingAspectPreset) {
    gParameterSuite->paramGetValueAtTime(instance->blankingAspectPreset, time, &preset);
  }
  if (instance->blankingAspectCustom) {
    gParameterSuite->paramGetValueAtTime(instance->blankingAspectCustom, time, &customAspect);
  }
  if (instance->blankingColour) {
    gParameterSuite->paramGetValueAtTime(instance->blankingColour, time,
                                         &colour[0], &colour[1], &colour[2], &colour[3]);
  }
  if (instance->blankingOpacity) {
    gParameterSuite->paramGetValueAtTime(instance->blankingOpacity, time, &opacity);
  }
  constexpr std::array<double, 4> presetAspects{1.78, 1.85, 2.00, 2.39};
  options.enabled = enabled != 0;
  options.editorialAspect = preset >= 0 && preset < 4
      ? presetAspects[static_cast<std::size_t>(preset)] : std::max(0.01, customAspect);
  options.outputPixelAspect = imagePAR(outputImage);
  options.outputPremultiplied = imageIsPremultiplied(outputImage);
  options.opacity = static_cast<float>(opacity);
  for (int channel = 0; channel < 4; ++channel) {
    options.colour[channel] = static_cast<float>(colour[channel]);
  }
  return options;
}

struct TextRenderSettings {
  wipreview::probe::TextOverlayOptions overlay;
  std::string text;
  std::string fontFamily;
  wipreview::text::FontStyle fontStyle = wipreview::text::FontStyle::Regular;
  double normalizedSize = 0.028;
  double pixelSize = 0.0;
  bool outlineEnabled = true;
  double normalizedOutlineWidth = 0.001;
  int outlineRadiusPixels = 0;
  float outlineColour[4] = {0.0F, 0.0F, 0.0F, 1.0F};
  float outlineOpacity = 1.0F;
  bool shadowEnabled = false;
  double normalizedShadowOffsetX = 0.0015;
  double normalizedShadowOffsetY = 0.0020;
  double normalizedShadowSoftness = 0.0020;
  int shadowOffsetXPixels = 0;
  int shadowOffsetDownPixels = 0;
  double shadowSoftnessPixels = 0.0;
  float shadowColour[4] = {0.0F, 0.0F, 0.0F, 1.0F};
  float shadowOpacity = 0.60F;
  double normalizedZoneGap = 0.010;
  wipreview::text::OverflowMode overflowMode =
      wipreview::text::OverflowMode::ShrinkToFit;
  double minimumFontScale = 0.60;
};

TextRenderSettings readGlobalTextSettings(const InstanceData* instance, OfxTime time,
                                          OfxPropertySetHandle outputImage,
                                          const wipreview::probe::ImageView& outputView) {
  TextRenderSettings settings;
  int style = 0;
  char* family = nullptr;
  double fontSize = 0.028;
  double colour[4] = {1.0, 1.0, 1.0, 1.0};
  double opacity = 1.0;
  int outlineEnabled = 1;
  double outlineWidth = 0.001;
  double outlineColour[4] = {0.0, 0.0, 0.0, 1.0};
  double outlineOpacity = 1.0;
  int shadowEnabled = 0;
  double shadowOffsetX = 0.0015;
  double shadowOffsetY = 0.0020;
  double shadowSoftness = 0.0020;
  double shadowColour[4] = {0.0, 0.0, 0.0, 1.0};
  double shadowOpacity = 0.60;
  double zoneGap = 0.010;
  int overflowMode = 2;
  double minimumFontScale = 0.60;
  double paddingLeft = 0.015;
  double paddingRight = 0.015;
  double paddingTop = 0.020;
  double paddingBottom = 0.020;
  if (instance->fontFamily) gParameterSuite->paramGetValueAtTime(instance->fontFamily, time, &family);
  if (instance->fontStyle) gParameterSuite->paramGetValueAtTime(instance->fontStyle, time, &style);
  if (instance->fontSize) gParameterSuite->paramGetValueAtTime(instance->fontSize, time, &fontSize);
  if (instance->textColour) {
    gParameterSuite->paramGetValueAtTime(instance->textColour, time,
                                         &colour[0], &colour[1], &colour[2], &colour[3]);
  }
  if (instance->textOpacity) gParameterSuite->paramGetValueAtTime(instance->textOpacity, time, &opacity);
  if (instance->outlineEnabled) {
    gParameterSuite->paramGetValueAtTime(instance->outlineEnabled, time, &outlineEnabled);
  }
  if (instance->outlineWidth) {
    gParameterSuite->paramGetValueAtTime(instance->outlineWidth, time, &outlineWidth);
  }
  if (instance->outlineColour) {
    gParameterSuite->paramGetValueAtTime(instance->outlineColour, time,
                                         &outlineColour[0], &outlineColour[1],
                                         &outlineColour[2], &outlineColour[3]);
  }
  if (instance->outlineOpacity) {
    gParameterSuite->paramGetValueAtTime(instance->outlineOpacity, time, &outlineOpacity);
  }
  if (instance->shadowEnabled) {
    gParameterSuite->paramGetValueAtTime(instance->shadowEnabled, time, &shadowEnabled);
  }
  if (instance->shadowOffsetX) {
    gParameterSuite->paramGetValueAtTime(instance->shadowOffsetX, time, &shadowOffsetX);
  }
  if (instance->shadowOffsetY) {
    gParameterSuite->paramGetValueAtTime(instance->shadowOffsetY, time, &shadowOffsetY);
  }
  if (instance->shadowSoftness) {
    gParameterSuite->paramGetValueAtTime(instance->shadowSoftness, time, &shadowSoftness);
  }
  if (instance->shadowColour) {
    gParameterSuite->paramGetValueAtTime(instance->shadowColour, time,
                                         &shadowColour[0], &shadowColour[1],
                                         &shadowColour[2], &shadowColour[3]);
  }
  if (instance->shadowOpacity) {
    gParameterSuite->paramGetValueAtTime(instance->shadowOpacity, time, &shadowOpacity);
  }
  if (instance->zoneGap) {
    gParameterSuite->paramGetValueAtTime(instance->zoneGap, time, &zoneGap);
  }
  if (instance->overflowMode) {
    gParameterSuite->paramGetValueAtTime(instance->overflowMode, time, &overflowMode);
  }
  if (instance->minimumFontScale) {
    gParameterSuite->paramGetValueAtTime(
        instance->minimumFontScale, time, &minimumFontScale);
  }
  if (instance->paddingLeft) gParameterSuite->paramGetValueAtTime(instance->paddingLeft, time, &paddingLeft);
  if (instance->paddingRight) gParameterSuite->paramGetValueAtTime(instance->paddingRight, time, &paddingRight);
  if (instance->paddingTop) gParameterSuite->paramGetValueAtTime(instance->paddingTop, time, &paddingTop);
  if (instance->paddingBottom) gParameterSuite->paramGetValueAtTime(instance->paddingBottom, time, &paddingBottom);

  const auto styles = std::array{
      wipreview::text::FontStyle::Regular,
      wipreview::text::FontStyle::Bold,
      wipreview::text::FontStyle::Italic,
      wipreview::text::FontStyle::BoldItalic};
  settings.overlay.enabled = false;
  settings.overlay.anchor = wipreview::probe::TextAnchor::TopLeft;
  settings.overlay.outputPremultiplied = imageIsPremultiplied(outputImage);
  settings.overlay.opacity = static_cast<float>(opacity);
  settings.overlay.paddingLeft = paddingLeft;
  settings.overlay.paddingRight = paddingRight;
  settings.overlay.paddingTop = paddingTop;
  settings.overlay.paddingBottom = paddingBottom;
  for (int channel = 0; channel < 4; ++channel) {
    settings.overlay.colour[channel] = static_cast<float>(colour[channel]);
  }
  settings.text.clear();
  settings.fontFamily = family ? family : "System Default";
  settings.fontStyle = styles[static_cast<std::size_t>(std::clamp(style, 0, 3))];
  settings.normalizedSize = std::clamp(fontSize, 0.001, 1.0);
  settings.pixelSize = settings.normalizedSize
                     * std::max(0, outputView.bounds.y2 - outputView.bounds.y1);
  settings.outlineEnabled = outlineEnabled != 0;
  settings.normalizedOutlineWidth = std::clamp(outlineWidth, 0.0, 0.010);
  const int outputHeight = std::max(0, outputView.bounds.y2 - outputView.bounds.y1);
  settings.outlineRadiusPixels = settings.outlineEnabled &&
      settings.normalizedOutlineWidth > 0.0
      ? std::max(1, static_cast<int>(std::lround(
            settings.normalizedOutlineWidth * outputHeight)))
      : 0;
  settings.outlineOpacity = static_cast<float>(outlineOpacity);
  for (int channel = 0; channel < 4; ++channel) {
    settings.outlineColour[channel] = static_cast<float>(outlineColour[channel]);
  }
  const int outputWidth = std::max(0, outputView.bounds.x2 - outputView.bounds.x1);
  settings.shadowEnabled = shadowEnabled != 0;
  settings.normalizedShadowOffsetX = std::clamp(shadowOffsetX, -0.05, 0.05);
  settings.normalizedShadowOffsetY = std::clamp(shadowOffsetY, -0.05, 0.05);
  settings.normalizedShadowSoftness = std::clamp(shadowSoftness, 0.0, 0.05);
  settings.shadowOffsetXPixels = static_cast<int>(std::lround(
      settings.normalizedShadowOffsetX * outputWidth));
  settings.shadowOffsetDownPixels = static_cast<int>(std::lround(
      settings.normalizedShadowOffsetY * outputHeight));
  settings.shadowSoftnessPixels = settings.normalizedShadowSoftness * outputHeight;
  settings.shadowOpacity = static_cast<float>(shadowOpacity);
  for (int channel = 0; channel < 4; ++channel) {
    settings.shadowColour[channel] = static_cast<float>(shadowColour[channel]);
  }
  const auto overflowModes = std::array{
      wipreview::text::OverflowMode::Clip,
      wipreview::text::OverflowMode::Ellipsis,
      wipreview::text::OverflowMode::ShrinkToFit};
  settings.normalizedZoneGap = std::clamp(zoneGap, 0.0, 0.25);
  settings.overflowMode = overflowModes[
      static_cast<std::size_t>(std::clamp(overflowMode, 0, 2))];
  settings.minimumFontScale = std::clamp(minimumFontScale, 0.01, 1.0);
  return settings;
}

struct ZoneTextSettings {
  TextRenderSettings layer;
  bool useSizeOverride = false;
  bool useColourOverride = false;
  bool useOpacityOverride = false;
};

ZoneTextSettings readZoneTextSettings(const InstanceData* instance, std::size_t index,
                                      OfxTime time, const TextRenderSettings& global,
                                      const wipreview::probe::ImageView& outputView) {
  ZoneTextSettings settings;
  settings.layer = global;
  settings.layer.overlay.enabled = false;
  settings.layer.overlay.anchor = static_cast<wipreview::probe::TextAnchor>(index);
  settings.layer.overlay.constrainToCell = true;
  settings.layer.overlay.cellBounds = wipreview::probe::computeTextCell(
      outputView.bounds, settings.layer.overlay.anchor,
      global.overlay.paddingLeft, global.overlay.paddingRight,
      global.normalizedZoneGap);
  settings.layer.overlay.offsetX = 0.0;
  settings.layer.overlay.offsetY = 0.0;
  settings.layer.text.clear();
  if (!instance || index >= instance->zones.size()) return settings;

  const auto& handles = instance->zones[index];
  int enabled = 0;
  int useSize = 0;
  int useColour = 0;
  int useOpacity = 0;
  char* text = nullptr;
  double size = global.normalizedSize;
  double colour[4] = {
      global.overlay.colour[0], global.overlay.colour[1],
      global.overlay.colour[2], global.overlay.colour[3]};
  double opacity = global.overlay.opacity;
  double offsetX = 0.0;
  double offsetY = 0.0;
  if (handles.enabled) gParameterSuite->paramGetValueAtTime(handles.enabled, time, &enabled);
  if (handles.text) gParameterSuite->paramGetValueAtTime(handles.text, time, &text);
  if (handles.useSize) gParameterSuite->paramGetValueAtTime(handles.useSize, time, &useSize);
  if (handles.size) gParameterSuite->paramGetValueAtTime(handles.size, time, &size);
  if (handles.useColour) gParameterSuite->paramGetValueAtTime(handles.useColour, time, &useColour);
  if (handles.colour) {
    gParameterSuite->paramGetValueAtTime(handles.colour, time,
                                         &colour[0], &colour[1], &colour[2], &colour[3]);
  }
  if (handles.useOpacity) gParameterSuite->paramGetValueAtTime(handles.useOpacity, time, &useOpacity);
  if (handles.opacity) gParameterSuite->paramGetValueAtTime(handles.opacity, time, &opacity);
  if (handles.offsetX) gParameterSuite->paramGetValueAtTime(handles.offsetX, time, &offsetX);
  if (handles.offsetY) gParameterSuite->paramGetValueAtTime(handles.offsetY, time, &offsetY);

  settings.useSizeOverride = useSize != 0;
  settings.useColourOverride = useColour != 0;
  settings.useOpacityOverride = useOpacity != 0;
  settings.layer.overlay.enabled = enabled != 0;
  settings.layer.text = text ? text : "";
  settings.layer.overlay.offsetX = std::clamp(offsetX, -1.0, 1.0);
  settings.layer.overlay.offsetY = std::clamp(offsetY, -1.0, 1.0);
  if (settings.useSizeOverride) settings.layer.normalizedSize = std::clamp(size, 0.001, 1.0);
  if (settings.useColourOverride) {
    for (int channel = 0; channel < 4; ++channel) {
      settings.layer.overlay.colour[channel] = static_cast<float>(colour[channel]);
    }
  }
  if (settings.useOpacityOverride) settings.layer.overlay.opacity = static_cast<float>(opacity);
  settings.layer.pixelSize = settings.layer.normalizedSize
                           * std::max(0, outputView.bounds.y2 - outputView.bounds.y1);
  return settings;
}

struct DynamicTextSettings {
  wipreview::tokens::Settings resolver;
  int fpsMode = 0;
  double hostFps = 0.0;
};

DynamicTextSettings readDynamicTextSettings(const InstanceData* instance) {
  DynamicTextSettings settings;
  int frameRelativeBase = 1;
  int frameStart = 1001;
  int fpsMode = 0;
  double fpsOverride = 24.0;
  char* timecodeStart = nullptr;
  int dropFrameMode = 0;
  if (instance->frameRelativeBase) {
    gParameterSuite->paramGetValue(instance->frameRelativeBase, &frameRelativeBase);
  }
  if (instance->frameStart) {
    gParameterSuite->paramGetValue(instance->frameStart, &frameStart);
  }
  if (instance->fpsMode) gParameterSuite->paramGetValue(instance->fpsMode, &fpsMode);
  if (instance->fpsOverride) {
    gParameterSuite->paramGetValue(instance->fpsOverride, &fpsOverride);
  }
  if (instance->timecodeStart) {
    gParameterSuite->paramGetValue(instance->timecodeStart, &timecodeStart);
  }
  if (instance->dropFrameMode) {
    gParameterSuite->paramGetValue(instance->dropFrameMode, &dropFrameMode);
  }
  OfxPropertySetHandle effectProperties = nullptr;
  gImageSuite->getPropertySet(instance->effect, &effectProperties);
  gPropertySuite->propGetDouble(
      effectProperties, kOfxImageEffectPropFrameRate, 0, &settings.hostFps);
  settings.fpsMode = std::clamp(fpsMode, 0, 1);
  settings.resolver.frameRelativeBase = frameRelativeBase;
  settings.resolver.frameStart = frameStart;
  settings.resolver.fps = settings.fpsMode == 0 ? settings.hostFps : fpsOverride;
  settings.resolver.timecodeStart = timecodeStart ? timecodeStart : "00:00:00:00";
  const auto modes = std::array{
      wipreview::tokens::DropFrameMode::Auto,
      wipreview::tokens::DropFrameMode::NonDrop,
      wipreview::tokens::DropFrameMode::Drop};
  settings.resolver.dropFrameMode = modes[
      static_cast<std::size_t>(std::clamp(dropFrameMode, 0, 2))];
  return settings;
}

OfxStatus load() {
  if (!gHost || !gHost->fetchSuite) return kOfxStatErrMissingHostFeature;
  gImageSuite = static_cast<const OfxImageEffectSuiteV1*>(
      gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1));
  gPropertySuite = static_cast<const OfxPropertySuiteV1*>(
      gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1));
  gParameterSuite = static_cast<const OfxParameterSuiteV1*>(
      gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1));
  gMessageSuite = static_cast<const OfxMessageSuiteV2*>(
      gHost->fetchSuite(gHost->host, kOfxMessageSuite, 2));

  if (!gImageSuite || !gPropertySuite || !gParameterSuite) {
    return kOfxStatErrMissingHostFeature;
  }
  Logger::instance().write("SESSION_BEGIN",
      "probe_version=" WIPREVIEW_PROBE_VERSION
      " sdk_commit=3de640d6f645fe6e346acd57e568d8b0a5ae4574"
      " log_path=" + quoted(Logger::instance().path().c_str()));
  Logger::instance().write("SUITES",
      std::string("image_effect_v1=true property_v1=true parameter_v1=true message_v2=") +
      (gMessageSuite ? "true" : "false"));
  logHostCapabilities();
  return kOfxStatOK;
}

OfxStatus unload() {
  Logger::instance().write("SESSION_END");
  return kOfxStatOK;
}

enum class DescriptorProfile {
  GeneralPreferred,
  FilterOnly,
};

OfxStatus describe(OfxImageEffectHandle effect, DescriptorProfile profile) {
  OfxPropertySetHandle properties = nullptr;
  OfxStatus status = gImageSuite->getPropertySet(effect, &properties);
  if (status != kOfxStatOK) return status;

  const bool filterOnly = profile == DescriptorProfile::FilterOnly;
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0,
                               filterOnly ? "WIP Review Probe (P4 Filter Only)"
                                          : "WIP Review Probe (P4)");
  gPropertySuite->propSetString(properties, kOfxPropShortLabel, 0,
                               filterOnly ? "WIP Probe Filter" : "WIP Probe");
  gPropertySuite->propSetString(properties, kOfxPropLongLabel, 0,
                               filterOnly ? "WIP Review Managed Color P4 — Filter Only"
                                          : "WIP Review Managed Color P4");
  gPropertySuite->propSetString(properties, kOfxPropPluginDescription, 0,
      "P4 managed-color overlay: review-raster geometry, display-light linear blanking and six styled dynamic text zones.");
  gPropertySuite->propSetString(properties, kOfxImageEffectPluginPropGrouping, 0,
                               "WIP Review/Diagnostics");
  gPropertySuite->propSetString(properties, kOfxImageEffectPropSupportedContexts, 0,
                               kOfxImageEffectContextFilter);
  if (!filterOnly) {
    gPropertySuite->propSetString(properties, kOfxImageEffectPropSupportedContexts, 1,
                                 kOfxImageEffectContextGeneral);
  }
  gPropertySuite->propSetString(properties, kOfxImageEffectPropSupportedPixelDepths, 0,
                               kOfxBitDepthByte);
  gPropertySuite->propSetString(properties, kOfxImageEffectPropSupportedPixelDepths, 1,
                               kOfxBitDepthShort);
  gPropertySuite->propSetString(properties, kOfxImageEffectPropSupportedPixelDepths, 2,
                               kOfxBitDepthHalf);
  gPropertySuite->propSetString(properties, kOfxImageEffectPropSupportedPixelDepths, 3,
                               kOfxBitDepthFloat);
  gPropertySuite->propSetInt(properties, kOfxImageEffectPropSupportsMultiResolution, 0, 1);
  gPropertySuite->propSetInt(properties, kOfxImageEffectPropSupportsTiles, 0, 0);
  gPropertySuite->propSetInt(properties, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
  gPropertySuite->propSetInt(properties, kOfxImageEffectPropSupportsMultipleClipPARs, 0, 1);
  gPropertySuite->propSetInt(properties, kOfxImageEffectPropTemporalClipAccess, 0, 0);
  gPropertySuite->propSetString(properties, kOfxImageEffectPluginRenderThreadSafety, 0,
                               kOfxImageEffectRenderFullySafe);
  gPropertySuite->propSetInt(properties, kOfxImageEffectPluginPropHostFrameThreading, 0, 1);
  for (std::size_t index = 0; index < kZoneParams.size(); ++index) {
    gPropertySuite->propSetString(
        properties, kOfxImageEffectPropClipPreferencesSlaveParam,
        static_cast<int>(index), kZoneParams[index].text);
  }
  gPropertySuite->propSetString(
      properties, kOfxImageEffectPropClipPreferencesSlaveParam,
      static_cast<int>(kZoneParams.size()), kParamColorSpaceMode);
  gPropertySuite->propSetString(
      properties, kOfxImageEffectPropClipPreferencesSlaveParam,
      static_cast<int>(kZoneParams.size() + 1), kParamManualColorSpace);

  const OfxStatus colourStyleStatus = gPropertySuite->propSetString(
      properties, kOfxImageEffectPropColourManagementStyle, 0,
      kOfxImageEffectColourManagementOCIO);
  const OfxStatus configStatus = gPropertySuite->propSetString(
      properties, kOfxImageEffectPropColourManagementAvailableConfigs, 0, kNativeConfig);
  Logger::instance().write("DESCRIBE",
      std::string("descriptor_profile=") + (filterOnly ? "FilterOnly" : "GeneralPreferred") +
      (filterOnly ? " declared_contexts=[Filter]" : " declared_contexts=[Filter,General]") +
      " multi_resolution=true tiles=false" +
      " colour_style_request=OCIO colour_style_status=" + statusName(colourStyleStatus) +
      " native_config_status=" + statusName(configStatus));
  return kOfxStatOK;
}

void defineStringParam(OfxParamSetHandle params, const char* name, const char* label,
                       const char* defaultValue, bool animates, const char* hint,
                       const char* parent = nullptr) {
  OfxPropertySetHandle properties = nullptr;
  if (gParameterSuite->paramDefine(params, kOfxParamTypeString, name, &properties) != kOfxStatOK) return;
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, label);
  gPropertySuite->propSetString(properties, kOfxParamPropDefault, 0, defaultValue);
  gPropertySuite->propSetString(properties, kOfxParamPropStringMode, 0, kOfxParamStringIsSingleLine);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, animates ? 1 : 0);
  gPropertySuite->propSetString(properties, kOfxParamPropHint, 0, hint);
  if (parent) gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, parent);
}

void defineDoubleParam(OfxParamSetHandle params, const char* name, const char* label,
                       double defaultValue, double minimum, double maximum,
                       double displayMinimum, double displayMaximum,
                       const char* hint, const char* parent = nullptr) {
  OfxPropertySetHandle properties = nullptr;
  if (gParameterSuite->paramDefine(params, kOfxParamTypeDouble, name, &properties) != kOfxStatOK) return;
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, label);
  gPropertySuite->propSetDouble(properties, kOfxParamPropDefault, 0, defaultValue);
  gPropertySuite->propSetDouble(properties, kOfxParamPropMin, 0, minimum);
  gPropertySuite->propSetDouble(properties, kOfxParamPropMax, 0, maximum);
  gPropertySuite->propSetDouble(properties, kOfxParamPropDisplayMin, 0, displayMinimum);
  gPropertySuite->propSetDouble(properties, kOfxParamPropDisplayMax, 0, displayMaximum);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  if (hint) gPropertySuite->propSetString(properties, kOfxParamPropHint, 0, hint);
  if (parent) gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, parent);
}

void defineIntegerParam(OfxParamSetHandle params, const char* name, const char* label,
                        int defaultValue, int minimum, int maximum,
                        int displayMinimum, int displayMaximum,
                        const char* hint, const char* parent = nullptr) {
  OfxPropertySetHandle properties = nullptr;
  if (gParameterSuite->paramDefine(
          params, kOfxParamTypeInteger, name, &properties) != kOfxStatOK) return;
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, label);
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, defaultValue);
  gPropertySuite->propSetInt(properties, kOfxParamPropMin, 0, minimum);
  gPropertySuite->propSetInt(properties, kOfxParamPropMax, 0, maximum);
  gPropertySuite->propSetInt(properties, kOfxParamPropDisplayMin, 0, displayMinimum);
  gPropertySuite->propSetInt(properties, kOfxParamPropDisplayMax, 0, displayMaximum);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  if (hint) gPropertySuite->propSetString(properties, kOfxParamPropHint, 0, hint);
  if (parent) gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, parent);
}

void defineZoneParams(OfxParamSetHandle params, const ZoneParamNames& zone) {
  OfxPropertySetHandle properties = nullptr;
  if (gParameterSuite->paramDefine(params, kOfxParamTypeGroup, zone.group,
                                   &properties) != kOfxStatOK) return;
  const std::string groupLabel = std::string(zone.label) + " Zone";
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, groupLabel.c_str());
  gPropertySuite->propSetInt(properties, kOfxParamPropGroupOpen, 0, 0);

  gParameterSuite->paramDefine(params, kOfxParamTypeBoolean, zone.enabled, &properties);
  const std::string enabledLabel = std::string(zone.label) + " Enabled";
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, enabledLabel.c_str());
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, 0);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, zone.group);

  const std::string textLabel = std::string(zone.label) + " Text";
  defineStringParam(params, zone.text, textLabel.c_str(), "", false,
                    "UTF-8 text. Supports {frame_rel}, {frame} and {timecode}.", zone.group);

  gParameterSuite->paramDefine(params, kOfxParamTypeBoolean, zone.useSize, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Use Size Override");
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, 0);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, zone.group);
  defineDoubleParam(params, zone.size, "Size Override", 0.028,
                    0.001, 1.0, 0.005, 0.10,
                    "Normalized to output height; used only when override is enabled.",
                    zone.group);

  gParameterSuite->paramDefine(params, kOfxParamTypeBoolean, zone.useColour, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Use Color Override");
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, 0);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, zone.group);
  gParameterSuite->paramDefine(params, kOfxParamTypeRGBA, zone.colour, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Color Override");
  const double colourDefault[4] = {1.0, 1.0, 1.0, 1.0};
  gPropertySuite->propSetDoubleN(properties, kOfxParamPropDefault, 4, colourDefault);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, zone.group);

  gParameterSuite->paramDefine(params, kOfxParamTypeBoolean, zone.useOpacity, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Use Opacity Override");
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, 0);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, zone.group);
  defineDoubleParam(params, zone.opacity, "Opacity Override", 1.0,
                    0.0, 1.0, 0.0, 1.0,
                    "Replaces global text opacity when override is enabled.", zone.group);

  defineDoubleParam(params, zone.offsetX, "Offset X", 0.0,
                    -1.0, 1.0, -0.25, 0.25,
                    "Normalized to output width; positive values move right.", zone.group);
  defineDoubleParam(params, zone.offsetY, "Offset Y", 0.0,
                    -1.0, 1.0, -0.25, 0.25,
                    "Normalized to output height; positive values move up.", zone.group);
}

OfxStatus describeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs) {
  OfxPropertySetHandle sourceProperties = nullptr;
  OfxPropertySetHandle outputProperties = nullptr;
  OfxStatus status = gImageSuite->clipDefine(effect, kOfxImageEffectSimpleSourceClipName,
                                             &sourceProperties);
  if (status != kOfxStatOK) return status;
  status = gImageSuite->clipDefine(effect, kOfxImageEffectOutputClipName, &outputProperties);
  if (status != kOfxStatOK) return status;

  for (OfxPropertySetHandle clip : {sourceProperties, outputProperties}) {
    gPropertySuite->propSetString(clip, kOfxImageEffectPropSupportedComponents, 0,
                                 kOfxImageComponentRGBA);
    gPropertySuite->propSetString(clip, kOfxImageEffectPropSupportedComponents, 1,
                                 kOfxImageComponentRGB);
    gPropertySuite->propSetInt(clip, kOfxImageEffectPropSupportsTiles, 0, 0);
  }
  gPropertySuite->propSetInt(sourceProperties, kOfxImageClipPropOptional, 0, 0);

  OfxParamSetHandle params = nullptr;
  status = gImageSuite->getParamSet(effect, &params);
  if (status != kOfxStatOK) return status;

  OfxPropertySetHandle properties = nullptr;
  gParameterSuite->paramDefine(params, kOfxParamTypeBoolean, kParamRequestRod, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Request Custom Output RoD");
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, 1);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropHint, 0,
      "P0 diagnostic gate. When enabled with Requested Review Raster, requests the width and height below.");

  defineChoiceParam(params, kParamCanvasMode, "Canvas Mode",
                    {"Host Raster", "Requested Review Raster"}, 1,
                    "Host Raster keeps the host output size; Requested Review Raster uses Requested Width/Height when the host accepts plugin RoD.");

  defineChoiceParam(params, kParamPlacement, "Placement",
                    {"Identity", "Fit", "Fill / Crop", "Stretch", "1:1"}, 1,
                    "Static source placement inside the output canvas. Identity is coordinate-aligned and never resizes.");

  defineChoiceParam(params, kParamResample, "Resample Filter",
                    {"Bilinear", "Bicubic (Catmull-Rom)", "Lanczos3"}, 2,
                    "CPU reference resampling filter. No sharpening is applied.");

  gParameterSuite->paramDefine(
      params, kOfxParamTypeGroup, kParamColorGroup, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Managed Color");
  gPropertySuite->propSetInt(properties, kOfxParamPropGroupOpen, 0, 1);
  defineChoiceParam(params, kParamColorSpaceMode, "Color Space Mode",
                    {"Auto from Host", "Manual Override"}, 0,
                    "Input and Output use the same display-referred review space. Auto requires a valid host colourspace.",
                    kParamColorGroup);
  defineChoiceParam(params, kParamManualColorSpace, "Manual Color Space",
                    {"Rec.709 Gamma 2.4", "Rec.2100 PQ", "Rec.2100 HLG"}, 0,
                    "Explicit interpretation used in Manual Override or when Auto cannot identify the host colourspace.",
                    kParamColorGroup);
  defineChoiceParam(params, kParamGraphicsWhiteMode, "Graphics White Mode",
                    {"Auto", "Manual"}, 0,
                    "Auto uses 100 nits for Rec.709, 203 nits for PQ, and 20.3 percent of HLG peak.",
                    kParamColorGroup);
  defineDoubleParam(params, kParamGraphicsWhiteNits, "Graphics White Nits", 203.0,
                    1.0, 10000.0, 48.0, 1000.0,
                    "Reference white for graphic picker value 1.0 when Graphics White Mode is Manual.",
                    kParamColorGroup);
  defineDoubleParam(params, kParamHlgPeakNits, "HLG Peak Nits", 1000.0,
                    100.0, 10000.0, 400.0, 4000.0,
                    "Peak display luminance used by the Rec.2100 HLG OOTF and automatic Graphics White.",
                    kParamColorGroup);

  gParameterSuite->paramDefine(params, kOfxParamTypeRGBA, kParamCanvasColour, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Canvas Colour");
  const double canvasDefault[4] = {0.0, 0.0, 0.0, 1.0};
  gPropertySuite->propSetDoubleN(properties, kOfxParamPropDefault, 4, canvasDefault);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropHint, 0,
      "RGBA canvas outside the placed source; defaults to opaque black.");

  gParameterSuite->paramDefine(params, kOfxParamTypeBoolean, kParamBlankingEnabled, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Blanking Enabled");
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, 0);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropHint, 0,
      "Composites editorial blanking outside the centred aperture without changing the raster.");

  defineChoiceParam(params, kParamBlankingAspectPreset, "Blanking Aspect",
                    {"1.78", "1.85", "2.00", "2.39", "Custom"}, 3,
                    "Editorial display aspect. Wider than the canvas creates letterbox; narrower creates pillarbox.");

  gParameterSuite->paramDefine(params, kOfxParamTypeDouble, kParamBlankingAspectCustom, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Blanking Custom Aspect");
  gPropertySuite->propSetDouble(properties, kOfxParamPropDefault, 0, 2.0);
  gPropertySuite->propSetDouble(properties, kOfxParamPropMin, 0, 0.1);
  gPropertySuite->propSetDouble(properties, kOfxParamPropMax, 0, 10.0);
  gPropertySuite->propSetDouble(properties, kOfxParamPropDisplayMin, 0, 1.0);
  gPropertySuite->propSetDouble(properties, kOfxParamPropDisplayMax, 0, 3.0);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);

  gParameterSuite->paramDefine(params, kOfxParamTypeRGBA, kParamBlankingColour, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Blanking Colour");
  const double blankingColourDefault[4] = {0.0, 0.0, 0.0, 1.0};
  gPropertySuite->propSetDoubleN(properties, kOfxParamPropDefault, 4, blankingColourDefault);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);

  gParameterSuite->paramDefine(params, kOfxParamTypeDouble, kParamBlankingOpacity, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Blanking Opacity");
  gPropertySuite->propSetDouble(properties, kOfxParamPropDefault, 0, 1.0);
  gPropertySuite->propSetDouble(properties, kOfxParamPropMin, 0, 0.0);
  gPropertySuite->propSetDouble(properties, kOfxParamPropMax, 0, 1.0);
  gPropertySuite->propSetDouble(properties, kOfxParamPropDisplayMin, 0, 0.0);
  gPropertySuite->propSetDouble(properties, kOfxParamPropDisplayMax, 0, 1.0);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);

  gParameterSuite->paramDefine(params, kOfxParamTypeGroup, kParamTextGroup, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Text Global");
  gPropertySuite->propSetInt(properties, kOfxParamPropGroupOpen, 0, 1);

  defineStringParam(params, kParamFontFamily, "Font Family", "System Default", false,
                    "CoreText font family. Missing fonts fall back to the macOS system font.",
                    kParamTextGroup);
  defineChoiceParam(params, kParamFontStyle, "Font Style",
                    {"Regular", "Bold", "Italic", "Bold Italic"}, 0,
                    "Requested CoreText symbolic style; unavailable traits fall back safely.",
                    kParamTextGroup);
  defineDoubleParam(params, kParamFontSize, "Font Size", 0.028, 0.001, 1.0, 0.005, 0.10,
                    "Normalized to output pixel height; 0.028 is approximately 2.8 percent.",
                    kParamTextGroup);

  gParameterSuite->paramDefine(params, kOfxParamTypeRGBA, kParamTextColour, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Text Colour");
  const double textColourDefault[4] = {1.0, 1.0, 1.0, 1.0};
  gPropertySuite->propSetDoubleN(properties, kOfxParamPropDefault, 4, textColourDefault);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, kParamTextGroup);

  defineDoubleParam(params, kParamTextOpacity, "Text Opacity", 1.0, 0.0, 1.0, 0.0, 1.0,
                    "Multiplies text alpha.", kParamTextGroup);

  defineDoubleParam(params, kParamPaddingLeft, "Padding Left", 0.015,
                    0.0, 1.0, 0.0, 0.25,
                    "Normalized outer left padding.", kParamTextGroup);
  defineDoubleParam(params, kParamPaddingRight, "Padding Right", 0.015,
                    0.0, 1.0, 0.0, 0.25,
                    "Normalized outer right padding.", kParamTextGroup);
  defineDoubleParam(params, kParamPaddingTop, "Padding Top", 0.020,
                    0.0, 1.0, 0.0, 0.25,
                    "Normalized to output height.", kParamTextGroup);
  defineDoubleParam(params, kParamPaddingBottom, "Padding Bottom", 0.020,
                    0.0, 1.0, 0.0, 0.25,
                    "Normalized to output height.", kParamTextGroup);
  defineDoubleParam(params, kParamZoneGap, "Zone Gap", 0.010,
                    0.0, 0.25, 0.0, 0.10,
                    "Complete normalized horizontal gap between adjacent logical cells.",
                    kParamTextGroup);
  defineChoiceParam(params, kParamOverflowMode, "Overflow Mode",
                    {"Clip", "Ellipsis", "ShrinkToFit"}, 2,
                    "Clip at the cell, replace the tail with an ellipsis, or reduce only overflowing text.",
                    kParamTextGroup);
  defineDoubleParam(params, kParamMinimumFontScale, "Minimum Font Scale", 0.60,
                    0.01, 1.0, 0.10, 1.0,
                    "Lowest scale permitted by ShrinkToFit; remaining overflow is clipped.",
                    kParamTextGroup);

  gParameterSuite->paramDefine(params, kOfxParamTypeGroup, kParamOutlineGroup, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Outline");
  gPropertySuite->propSetInt(properties, kOfxParamPropGroupOpen, 0, 0);

  gParameterSuite->paramDefine(params, kOfxParamTypeBoolean, kParamOutlineEnabled, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Outline Enabled");
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, 1);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, kParamOutlineGroup);
  defineDoubleParam(params, kParamOutlineWidth, "Outline Width", 0.0010,
                    0.0, 0.010, 0.0, 0.010,
                    "Glyph-mask dilation radius normalized to output height.",
                    kParamOutlineGroup);
  gParameterSuite->paramDefine(params, kOfxParamTypeRGBA, kParamOutlineColour, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Outline Colour");
  const double outlineColourDefault[4] = {0.0, 0.0, 0.0, 1.0};
  gPropertySuite->propSetDoubleN(properties, kOfxParamPropDefault, 4,
                                outlineColourDefault);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, kParamOutlineGroup);
  defineDoubleParam(params, kParamOutlineOpacity, "Outline Opacity", 1.0,
                    0.0, 1.0, 0.0, 1.0,
                    "Multiplies outline alpha independently of text opacity.",
                    kParamOutlineGroup);

  gParameterSuite->paramDefine(params, kOfxParamTypeGroup, kParamShadowGroup, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Drop Shadow");
  gPropertySuite->propSetInt(properties, kOfxParamPropGroupOpen, 0, 0);

  gParameterSuite->paramDefine(params, kOfxParamTypeBoolean, kParamShadowEnabled, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Shadow Enabled");
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, 0);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, kParamShadowGroup);
  defineDoubleParam(params, kParamShadowOffsetX, "Shadow Offset X", 0.0015,
                    -0.05, 0.05, -0.05, 0.05,
                    "Normalized to output width; positive values move right.",
                    kParamShadowGroup);
  defineDoubleParam(params, kParamShadowOffsetY, "Shadow Offset Y", 0.0020,
                    -0.05, 0.05, -0.05, 0.05,
                    "Normalized to output height; positive values move visually down.",
                    kParamShadowGroup);
  defineDoubleParam(params, kParamShadowSoftness, "Shadow Softness", 0.0020,
                    0.0, 0.05, 0.0, 0.05,
                    "Gaussian sigma normalized to output height; applied to shadow alpha.",
                    kParamShadowGroup);
  gParameterSuite->paramDefine(params, kOfxParamTypeRGBA, kParamShadowColour, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Shadow Colour");
  const double shadowColourDefault[4] = {0.0, 0.0, 0.0, 1.0};
  gPropertySuite->propSetDoubleN(properties, kOfxParamPropDefault, 4,
                                shadowColourDefault);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);
  gPropertySuite->propSetString(properties, kOfxParamPropParent, 0, kParamShadowGroup);
  defineDoubleParam(params, kParamShadowOpacity, "Shadow Opacity", 0.60,
                    0.0, 1.0, 0.0, 1.0,
                    "Multiplies shadow alpha independently of fill and outline.",
                    kParamShadowGroup);
  for (const auto& zone : kZoneParams) defineZoneParams(params, zone);

  gParameterSuite->paramDefine(
      params, kOfxParamTypeGroup, kParamDynamicTextGroup, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Dynamic Text");
  gPropertySuite->propSetInt(properties, kOfxParamPropGroupOpen, 0, 0);
  defineIntegerParam(params, kParamFrameRelativeBase, "Frame Relative Base", 1,
                     -1000000000, 1000000000, -10000, 10000,
                     "Added to round(effectTime) for {frame_rel}.",
                     kParamDynamicTextGroup);
  defineIntegerParam(params, kParamFrameStart, "Frame Start", 1001,
                     -1000000000, 1000000000, -10000, 100000,
                     "Added to round(effectTime) for {frame}; set explicitly from workflow data.",
                     kParamDynamicTextGroup);
  defineChoiceParam(params, kParamFpsMode, "FPS Mode",
                    {"AutoFromHost", "Override"}, 0,
                    "Use the effect frame rate published by the host or FPS Override.",
                    kParamDynamicTextGroup);
  defineDoubleParam(params, kParamFpsOverride, "FPS Override", 24.0,
                    1.0, 240.0, 1.0, 120.0,
                    "Frame rate used for {timecode} when FPS Mode is Override.",
                    kParamDynamicTextGroup);
  defineStringParam(params, kParamTimecodeStart, "Timecode Start", "00:00:00:00",
                    false, "HH:MM:SS:FF or HH:MM:SS;FF start value.",
                    kParamDynamicTextGroup);
  defineChoiceParam(params, kParamDropFrameMode, "Drop Frame Mode",
                    {"Auto", "NonDrop", "Drop"}, 0,
                    "Auto enables drop-frame for 29.97/59.94; incompatible Drop requests are logged and use NonDrop.",
                    kParamDynamicTextGroup);

  gParameterSuite->paramDefine(params, kOfxParamTypeInteger, kParamWidth, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Requested Width");
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, 1920);
  gPropertySuite->propSetInt(properties, kOfxParamPropMin, 0, 1);
  gPropertySuite->propSetInt(properties, kOfxParamPropMax, 0, 32768);
  gPropertySuite->propSetInt(properties, kOfxParamPropDisplayMin, 0, 16);
  gPropertySuite->propSetInt(properties, kOfxParamPropDisplayMax, 0, 8192);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);

  gParameterSuite->paramDefine(params, kOfxParamTypeInteger, kParamHeight, &properties);
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, "Requested Height");
  gPropertySuite->propSetInt(properties, kOfxParamPropDefault, 0, 1080);
  gPropertySuite->propSetInt(properties, kOfxParamPropMin, 0, 1);
  gPropertySuite->propSetInt(properties, kOfxParamPropMax, 0, 32768);
  gPropertySuite->propSetInt(properties, kOfxParamPropDisplayMin, 0, 16);
  gPropertySuite->propSetInt(properties, kOfxParamPropDisplayMax, 0, 8192);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, 0);

  defineStringParam(params, kParamScenario, "Scenario Label",
                    "UNSET — name Edit Filter / Color Filter / Fusion General / Fusion Filter",
                    false, "Written into every record so the four mandatory cases remain separable.");
  defineStringParam(params, kParamAnimatedString, "Animated String Probe", "P0 string value",
                    true, "Animate this value on two frames; P0 logs value-at-time, key count and is-animating.");

  const std::string context = getString(inArgs, kOfxImageEffectPropContext);
  Logger::instance().write("DESCRIBE_IN_CONTEXT", "context=" + context);
  return kOfxStatOK;
}

OfxStatus createInstance(OfxImageEffectHandle effect) {
  auto* instance = new InstanceData;
  instance->effect = effect;
  instance->id = gNextInstanceId.fetch_add(1);

  OfxPropertySetHandle effectProperties = nullptr;
  OfxParamSetHandle params = nullptr;
  gImageSuite->getPropertySet(effect, &effectProperties);
  gImageSuite->getParamSet(effect, &params);
  char* context = nullptr;
  if (gPropertySuite->propGetString(effectProperties, kOfxImageEffectPropContext, 0, &context) == kOfxStatOK && context) {
    instance->context = context;
  }

  gImageSuite->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &instance->source, nullptr);
  gImageSuite->clipGetHandle(effect, kOfxImageEffectOutputClipName, &instance->output, nullptr);
  gParameterSuite->paramGetHandle(params, kParamRequestRod, &instance->requestRod, nullptr);
  gParameterSuite->paramGetHandle(params, kParamWidth, &instance->width, nullptr);
  gParameterSuite->paramGetHandle(params, kParamHeight, &instance->height, nullptr);
  gParameterSuite->paramGetHandle(params, kParamScenario, &instance->scenario, nullptr);
  gParameterSuite->paramGetHandle(params, kParamAnimatedString, &instance->animatedString, nullptr);
  gParameterSuite->paramGetHandle(params, kParamCanvasMode, &instance->canvasMode, nullptr);
  gParameterSuite->paramGetHandle(params, kParamPlacement, &instance->placement, nullptr);
  gParameterSuite->paramGetHandle(params, kParamResample, &instance->resample, nullptr);
  gParameterSuite->paramGetHandle(params, kParamCanvasColour, &instance->canvasColour, nullptr);
  gParameterSuite->paramGetHandle(
      params, kParamColorSpaceMode, &instance->colorSpaceMode, nullptr);
  gParameterSuite->paramGetHandle(
      params, kParamManualColorSpace, &instance->manualColorSpace, nullptr);
  gParameterSuite->paramGetHandle(
      params, kParamGraphicsWhiteMode, &instance->graphicsWhiteMode, nullptr);
  gParameterSuite->paramGetHandle(
      params, kParamGraphicsWhiteNits, &instance->graphicsWhiteNits, nullptr);
  gParameterSuite->paramGetHandle(
      params, kParamHlgPeakNits, &instance->hlgPeakNits, nullptr);
  gParameterSuite->paramGetHandle(params, kParamBlankingEnabled, &instance->blankingEnabled, nullptr);
  gParameterSuite->paramGetHandle(params, kParamBlankingAspectPreset, &instance->blankingAspectPreset, nullptr);
  gParameterSuite->paramGetHandle(params, kParamBlankingAspectCustom, &instance->blankingAspectCustom, nullptr);
  gParameterSuite->paramGetHandle(params, kParamBlankingColour, &instance->blankingColour, nullptr);
  gParameterSuite->paramGetHandle(params, kParamBlankingOpacity, &instance->blankingOpacity, nullptr);
  gParameterSuite->paramGetHandle(params, kParamFontFamily, &instance->fontFamily, nullptr);
  gParameterSuite->paramGetHandle(params, kParamFontStyle, &instance->fontStyle, nullptr);
  gParameterSuite->paramGetHandle(params, kParamFontSize, &instance->fontSize, nullptr);
  gParameterSuite->paramGetHandle(params, kParamTextColour, &instance->textColour, nullptr);
  gParameterSuite->paramGetHandle(params, kParamTextOpacity, &instance->textOpacity, nullptr);
  gParameterSuite->paramGetHandle(params, kParamZoneGap, &instance->zoneGap, nullptr);
  gParameterSuite->paramGetHandle(params, kParamOverflowMode, &instance->overflowMode, nullptr);
  gParameterSuite->paramGetHandle(
      params, kParamMinimumFontScale, &instance->minimumFontScale, nullptr);
  gParameterSuite->paramGetHandle(
      params, kParamFrameRelativeBase, &instance->frameRelativeBase, nullptr);
  gParameterSuite->paramGetHandle(params, kParamFrameStart, &instance->frameStart, nullptr);
  gParameterSuite->paramGetHandle(params, kParamFpsMode, &instance->fpsMode, nullptr);
  gParameterSuite->paramGetHandle(params, kParamFpsOverride, &instance->fpsOverride, nullptr);
  gParameterSuite->paramGetHandle(
      params, kParamTimecodeStart, &instance->timecodeStart, nullptr);
  gParameterSuite->paramGetHandle(
      params, kParamDropFrameMode, &instance->dropFrameMode, nullptr);
  gParameterSuite->paramGetHandle(params, kParamOutlineEnabled, &instance->outlineEnabled, nullptr);
  gParameterSuite->paramGetHandle(params, kParamOutlineWidth, &instance->outlineWidth, nullptr);
  gParameterSuite->paramGetHandle(params, kParamOutlineColour, &instance->outlineColour, nullptr);
  gParameterSuite->paramGetHandle(params, kParamOutlineOpacity, &instance->outlineOpacity, nullptr);
  gParameterSuite->paramGetHandle(params, kParamShadowEnabled, &instance->shadowEnabled, nullptr);
  gParameterSuite->paramGetHandle(params, kParamShadowOffsetX, &instance->shadowOffsetX, nullptr);
  gParameterSuite->paramGetHandle(params, kParamShadowOffsetY, &instance->shadowOffsetY, nullptr);
  gParameterSuite->paramGetHandle(params, kParamShadowSoftness, &instance->shadowSoftness, nullptr);
  gParameterSuite->paramGetHandle(params, kParamShadowColour, &instance->shadowColour, nullptr);
  gParameterSuite->paramGetHandle(params, kParamShadowOpacity, &instance->shadowOpacity, nullptr);
  gParameterSuite->paramGetHandle(params, kParamPaddingLeft, &instance->paddingLeft, nullptr);
  gParameterSuite->paramGetHandle(params, kParamPaddingRight, &instance->paddingRight, nullptr);
  gParameterSuite->paramGetHandle(params, kParamPaddingTop, &instance->paddingTop, nullptr);
  gParameterSuite->paramGetHandle(params, kParamPaddingBottom, &instance->paddingBottom, nullptr);
  for (std::size_t index = 0; index < kZoneParams.size(); ++index) {
    const auto& names = kZoneParams[index];
    auto& handles = instance->zones[index];
    gParameterSuite->paramGetHandle(params, names.enabled, &handles.enabled, nullptr);
    gParameterSuite->paramGetHandle(params, names.text, &handles.text, nullptr);
    gParameterSuite->paramGetHandle(params, names.useSize, &handles.useSize, nullptr);
    gParameterSuite->paramGetHandle(params, names.size, &handles.size, nullptr);
    gParameterSuite->paramGetHandle(params, names.useColour, &handles.useColour, nullptr);
    gParameterSuite->paramGetHandle(params, names.colour, &handles.colour, nullptr);
    gParameterSuite->paramGetHandle(params, names.useOpacity, &handles.useOpacity, nullptr);
    gParameterSuite->paramGetHandle(params, names.opacity, &handles.opacity, nullptr);
    gParameterSuite->paramGetHandle(params, names.offsetX, &handles.offsetX, nullptr);
    gParameterSuite->paramGetHandle(params, names.offsetY, &handles.offsetY, nullptr);
  }
  gPropertySuite->propSetPointer(effectProperties, kOfxPropInstanceData, 0, instance);

  Logger::instance().write("INSTANCE_CREATE",
      instancePrefix(instance) + " log_path=" + quoted(Logger::instance().path().c_str()));
  logEffectProperties(instance, effectProperties);
  logClip(instance, "Source", instance->source, 0.0);
  return kOfxStatOK;
}

OfxStatus destroyInstance(OfxImageEffectHandle effect) {
  InstanceData* instance = getInstance(effect);
  Logger::instance().write("INSTANCE_DESTROY", instancePrefix(instance));
  OfxPropertySetHandle properties = nullptr;
  if (gImageSuite->getPropertySet(effect, &properties) == kOfxStatOK) {
    gPropertySuite->propSetPointer(properties, kOfxPropInstanceData, 0, nullptr);
  }
  delete instance;
  return kOfxStatOK;
}

OfxStatus getRegionOfDefinition(OfxImageEffectHandle effect,
                                OfxPropertySetHandle inArgs,
                                OfxPropertySetHandle outArgs) {
  InstanceData* instance = getInstance(effect);
  if (!instance) return kOfxStatErrBadHandle;
  double time = 0.0;
  gPropertySuite->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  bool request = true;
  int width = 1920;
  int height = 1080;
  readRodRequest(instance, request, width, height);

  double outputPAR = 1.0;
  OfxPropertySetHandle outputProperties = nullptr;
  if (gImageSuite->clipGetPropertySet(instance->output, &outputProperties) == kOfxStatOK) {
    const OfxStatus parStatus = gPropertySuite->propGetDouble(
        outputProperties, kOfxImagePropPixelAspectRatio, 0, &outputPAR);
    if (parStatus != kOfxStatOK || outputPAR <= 0.0) outputPAR = 1.0;
  }

  OfxRectD sourceRod{};
  const OfxStatus sourceStatus = gImageSuite->clipGetRegionOfDefinition(instance->source, time, &sourceRod);
  std::ostringstream fields;
  fields << instancePrefix(instance) << " time=" << time
         << " request_enabled=" << (request ? "true" : "false")
         << " requested_physical_raster=[" << width << ',' << height << ']'
         << " output_PAR=" << outputPAR
         << " requested_canonical_RoD=[0,0," << width * outputPAR << ',' << height << ']'
         << " source_RoD_status=" << statusName(sourceStatus);
  if (sourceStatus == kOfxStatOK) {
    fields << " source_RoD=[" << sourceRod.x1 << ',' << sourceRod.y1 << ','
           << sourceRod.x2 << ',' << sourceRod.y2 << ']';
  }
  Logger::instance().write("GET_REGION_OF_DEFINITION", fields.str());

  if (!request) return kOfxStatReplyDefault;
  const double requested[4] = {0.0, 0.0, static_cast<double>(width) * outputPAR,
                               static_cast<double>(height)};
  return gPropertySuite->propSetDoubleN(outArgs, kOfxImageEffectPropRegionOfDefinition, 4, requested);
}

OfxStatus getRegionsOfInterest(OfxImageEffectHandle effect,
                               OfxPropertySetHandle inArgs,
                               OfxPropertySetHandle outArgs) {
  InstanceData* instance = getInstance(effect);
  if (!instance) return kOfxStatErrBadHandle;
  double time = 0.0;
  gPropertySuite->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  OfxRectD sourceRod{};
  const OfxStatus status = gImageSuite->clipGetRegionOfDefinition(instance->source, time, &sourceRod);
  Logger::instance().write("GET_REGIONS_OF_INTEREST",
      instancePrefix(instance) + " time=" + getDouble(inArgs, kOfxPropTime) +
      " output_RoI=" + joinDoubles(inArgs, kOfxImageEffectPropRegionOfInterest) +
      " render_scale=" + joinDoubles(inArgs, kOfxImageEffectPropRenderScale) +
      " requested_Source_RoI=" +
      (status == kOfxStatOK
          ? ("[" + std::to_string(sourceRod.x1) + ',' + std::to_string(sourceRod.y1) + ',' +
             std::to_string(sourceRod.x2) + ',' + std::to_string(sourceRod.y2) + ']')
          : ("<" + std::string(statusName(status)) + ">")));
  if (status != kOfxStatOK) return kOfxStatReplyDefault;
  return gPropertySuite->propSetDoubleN(outArgs, "OfxImageClipPropRoI_Source", 4, &sourceRod.x1);
}

OfxStatus getClipPreferences(OfxImageEffectHandle effect,
                             OfxPropertySetHandle outArgs) {
  InstanceData* instance = getInstance(effect);
  if (!instance) return kOfxStatErrBadHandle;
  OfxPropertySetHandle sourceProperties = nullptr;
  gImageSuite->clipGetPropertySet(instance->source, &sourceProperties);
  char* components = nullptr;
  char* depth = nullptr;
  char* premultiplication = nullptr;
  const OfxStatus componentStatus = gPropertySuite->propGetString(
      sourceProperties, kOfxImageEffectPropComponents, 0, &components);
  const OfxStatus depthStatus = gPropertySuite->propGetString(
      sourceProperties, kOfxImageEffectPropPixelDepth, 0, &depth);
  const OfxStatus premultStatus = gPropertySuite->propGetString(
      sourceProperties, kOfxImageEffectPropPreMultiplication, 0, &premultiplication);
  if (componentStatus == kOfxStatOK) {
    gPropertySuite->propSetString(outArgs, "OfxImageClipPropComponents_Output", 0, components);
  }
  if (depthStatus == kOfxStatOK) {
    gPropertySuite->propSetString(outArgs, "OfxImageClipPropDepth_Output", 0, depth);
  }
  if (premultStatus == kOfxStatOK) {
    gPropertySuite->propSetString(outArgs, kOfxImageEffectPropPreMultiplication, 0, premultiplication);
  }
  int colorSpaceMode = 0;
  int manualColorSpace = 0;
  if (instance->colorSpaceMode) {
    gParameterSuite->paramGetValue(instance->colorSpaceMode, &colorSpaceMode);
  }
  if (instance->manualColorSpace) {
    gParameterSuite->paramGetValue(instance->manualColorSpace, &manualColorSpace);
  }
  OfxStatus preferredColourspaceStatus = kOfxStatReplyDefault;
  const auto manualEncodings = std::array{
      wipreview::color::DisplayEncoding::Rec709Gamma24,
      wipreview::color::DisplayEncoding::Rec2100PQ,
      wipreview::color::DisplayEncoding::Rec2100HLG};
  const auto manualEncoding = manualEncodings[static_cast<std::size_t>(
      std::clamp(manualColorSpace, 0, 2))];
  if (colorSpaceMode == 1) {
    preferredColourspaceStatus = gPropertySuite->propSetString(
        outArgs, kSourcePreferredColourspaces, 0,
        ofxDisplayColourspace(manualEncoding));
  }
  const OfxStatus outputPARStatus = gPropertySuite->propSetDouble(
      outArgs, kOutputClipPARPreference, 0, 1.0);
  bool outputFrameVarying = false;
  for (const auto& zone : instance->zones) {
    char* text = nullptr;
    if (zone.text && gParameterSuite->paramGetValue(zone.text, &text) == kOfxStatOK &&
        text && wipreview::tokens::containsDynamicToken(text)) {
      outputFrameVarying = true;
      break;
    }
  }
  gPropertySuite->propSetInt(
      outArgs, kOfxImageEffectFrameVarying, 0, outputFrameVarying ? 1 : 0);
  Logger::instance().write("GET_CLIP_PREFERENCES",
      instancePrefix(instance) + " source_components=" + quoted(components) +
      " source_depth=" + quoted(depth) +
      " source_premultiplication=" + quoted(premultiplication) +
      " requested_output_PAR=1 output_PAR_status=" + statusName(outputPARStatus) +
      " color_space_mode=" + std::to_string(std::clamp(colorSpaceMode, 0, 1)) +
      " manual_color_space=" + quoted(displayEncodingName(manualEncoding)) +
      " requested_source_colourspace=" +
          (colorSpaceMode == 1
              ? quoted(ofxDisplayColourspace(manualEncoding))
              : "<none>") +
      " preferred_colourspace_status=" +
          statusName(preferredColourspaceStatus) +
      " output_frame_varying=" + (outputFrameVarying ? "true" : "false") +
      " negotiated_style=" +
      getString([&] { OfxPropertySetHandle p = nullptr; gImageSuite->getPropertySet(effect, &p); return p; }(),
                kOfxImageEffectPropColourManagementStyle));
  return kOfxStatOK;
}

OfxStatus getOutputColourspace(OfxImageEffectHandle effect,
                               OfxPropertySetHandle inArgs,
                               OfxPropertySetHandle outArgs) {
  InstanceData* instance = getInstance(effect);
  if (!instance) return kOfxStatErrBadHandle;
  const OfxStatus status = gPropertySuite->propSetString(
      outArgs, kOfxImageClipPropColourspace, 0, "OfxColourspace_Source");
  Logger::instance().write("GET_OUTPUT_COLOURSPACE",
      instancePrefix(instance) +
      " host_preferred=" + joinStrings(inArgs, kOfxImageClipPropPreferredColourspaces) +
      " requested_output=\"OfxColourspace_Source\" status=" + statusName(status));
  return status;
}

OfxStatus getTimeDomain(OfxImageEffectHandle effect, OfxPropertySetHandle outArgs) {
  InstanceData* instance = getInstance(effect);
  if (!instance) return kOfxStatErrBadHandle;
  OfxPropertySetHandle sourceProperties = nullptr;
  gImageSuite->clipGetPropertySet(instance->source, &sourceProperties);
  double range[2]{};
  const OfxStatus status = gPropertySuite->propGetDoubleN(
      sourceProperties, kOfxImageEffectPropFrameRange, 2, range);
  Logger::instance().write("GET_TIME_DOMAIN",
      instancePrefix(instance) + " source_frame_range=" +
      joinDoubles(sourceProperties, kOfxImageEffectPropFrameRange));
  if (status != kOfxStatOK) return kOfxStatReplyDefault;
  return gPropertySuite->propSetDoubleN(outArgs, kOfxImageEffectPropFrameRange, 2, range);
}

void logTemporalParameters(const InstanceData* instance, OfxTime time) {
  char* scenario = nullptr;
  char* animated = nullptr;
  const OfxStatus scenarioStatus = gParameterSuite->paramGetValueAtTime(
      instance->scenario, time, &scenario);
  const OfxStatus animatedStatus = gParameterSuite->paramGetValueAtTime(
      instance->animatedString, time, &animated);
  unsigned int keyCount = 0;
  const OfxStatus keyStatus = gParameterSuite->paramGetNumKeys(instance->animatedString, &keyCount);
  OfxPropertySetHandle parameterProperties = nullptr;
  gParameterSuite->paramGetPropertySet(instance->animatedString, &parameterProperties);
  Logger::instance().write("TEMPORAL_STRING_PROBE",
      instancePrefix(instance) + " time=" + std::to_string(time) +
      " scenario=" + quoted(scenario) + " scenario_status=" + statusName(scenarioStatus) +
      " animated_value=" + quoted(animated) + " value_status=" + statusName(animatedStatus) +
      " is_animating=" + getInt(parameterProperties, kOfxParamPropIsAnimating) +
      " key_count=" + std::to_string(keyCount) + " key_status=" + statusName(keyStatus));
}

OfxStatus render(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs) {
  InstanceData* instance = getInstance(effect);
  if (!instance) return kOfxStatErrBadHandle;
  double time = 0.0;
  int renderWindow[4]{};
  gPropertySuite->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  gPropertySuite->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, renderWindow);
  Logger::instance().write("RENDER",
      instancePrefix(instance) + " time=" + getDouble(inArgs, kOfxPropTime) +
      " render_window=" + joinInts(inArgs, kOfxImageEffectPropRenderWindow) +
      " render_scale=" + joinDoubles(inArgs, kOfxImageEffectPropRenderScale) +
      " sequential=" + getInt(inArgs, kOfxImageEffectPropSequentialRenderStatus) +
      " interactive=" + getInt(inArgs, kOfxImageEffectPropInteractiveRenderStatus));
  logTemporalParameters(instance, time);
  logClip(instance, "Source", instance->source, time);
  logClip(instance, "Output", instance->output, time);

  OfxPropertySetHandle sourceImage = nullptr;
  OfxPropertySetHandle outputImage = nullptr;
  const OfxStatus sourceStatus = gImageSuite->clipGetImage(instance->source, time, nullptr, &sourceImage);
  const OfxStatus outputStatus = gImageSuite->clipGetImage(instance->output, time, nullptr, &outputImage);
  Logger::instance().write("IMAGE_FETCH",
      instancePrefix(instance) + " time=" + std::to_string(time) +
      " source_status=" + statusName(sourceStatus) +
      " output_status=" + statusName(outputStatus));

  if (sourceImage) logImage(instance, "Source", sourceImage);
  if (outputImage) logImage(instance, "Output", outputImage);

  OfxStatus result = kOfxStatOK;
  if (outputStatus != kOfxStatOK || !outputImage) {
    result = gImageSuite->abort(effect) ? kOfxStatOK : kOfxStatFailed;
  } else {
    const auto sourceView = sourceImage ? imageView(sourceImage) : wipreview::probe::ImageView{};
    const auto outputView = imageView(outputImage);
    const auto options = readRenderOptions(instance, time, sourceImage, outputImage);
    const auto managedColor = readManagedColorSettings(instance, sourceImage);
    if (gMessageSuite) {
      if (managedColor.colorSpaceMode == 0 && !managedColor.hostRecognized) {
        gMessageSuite->setPersistentMessage(
            effect, kOfxMessageWarning, "WIPReviewUnknownColorSpace",
            "Host colourspace '%s' is not a supported display space. Manual Color Space '%s' is being used.",
            managedColor.hostColourspace.c_str(),
            displayEncodingName(managedColor.config.encoding));
      } else {
        gMessageSuite->clearPersistentMessage(effect);
      }
    }
    const int outputWidth = std::max(
        0, outputView.bounds.x2 - outputView.bounds.x1);
    const int outputHeight = std::max(
        0, outputView.bounds.y2 - outputView.bounds.y1);
    std::vector<float> displayLinearPixels(
        static_cast<std::size_t>(outputWidth) *
        static_cast<std::size_t>(outputHeight) * 4U);
    const wipreview::probe::ImageView displayLinearView{
        reinterpret_cast<std::byte*>(displayLinearPixels.data()),
        outputView.bounds,
        static_cast<std::ptrdiff_t>(outputWidth * 4 * sizeof(float)),
        sizeof(float) * 4,
        4,
        wipreview::probe::ChannelType::Float32};
    const wipreview::probe::RectI requestedWindow{
        renderWindow[0], renderWindow[1], renderWindow[2], renderWindow[3]};
    wipreview::probe::renderManagedDisplayFrame(
        sourceView, displayLinearView, requestedWindow, options,
        managedColor.config);
    auto blanking = readBlankingOptions(instance, time, outputImage);
    blanking.outputPremultiplied = true;
    wipreview::probe::applyBlanking(
        displayLinearView, requestedWindow, blanking);
    const auto aperture = wipreview::probe::computeBlankingAperture(
        displayLinearView.bounds, blanking);
    auto globalText = readGlobalTextSettings(
        instance, time, outputImage, displayLinearView);
    globalText.overlay.outputPremultiplied = true;
    const auto dynamicText = readDynamicTextSettings(instance);
    std::array<ZoneTextSettings, 6> zoneSettings{};
    std::array<wipreview::tokens::Resolution, 6> zoneTokens{};
    std::array<wipreview::text::TextLayoutResult, 6> zoneLayouts{};
    std::array<wipreview::text::GlyphRaster, 6> zoneGlyphs{};
    std::array<wipreview::probe::PointI, 6> zoneOrigins{};
    bool zoneRasterizationFailed = false;
    bool outlineGenerationFailed = false;
    bool shadowGenerationFailed = false;
    bool timecodeResolutionFallback = false;
    for (std::size_t index = 0; index < zoneSettings.size(); ++index) {
      zoneSettings[index] = readZoneTextSettings(
          instance, index, time, globalText, displayLinearView);
      const auto& layer = zoneSettings[index].layer;
      if (layer.overlay.enabled) {
        zoneTokens[index] = wipreview::tokens::resolve(
            layer.text, time, dynamicText.resolver);
        timecodeResolutionFallback = timecodeResolutionFallback ||
            (layer.text.find("{timecode}") != std::string::npos &&
             zoneTokens[index].usedTimecodeFallback);
        wipreview::text::TextLayoutRequest layoutRequest;
        layoutRequest.text = zoneTokens[index].text;
        layoutRequest.fontFamily = layer.fontFamily;
        layoutRequest.fontStyle = layer.fontStyle;
        layoutRequest.overflowMode = layer.overflowMode;
        layoutRequest.requestedPixelSize = layer.pixelSize;
        layoutRequest.minimumFontScale = layer.minimumFontScale;
        layoutRequest.availableWidth = std::max(
            0, layer.overlay.cellBounds.x2 - layer.overlay.cellBounds.x1);
        layoutRequest.outlineRadiusPixels = layer.outlineRadiusPixels;
        layoutRequest.shadowEnabled = layer.shadowEnabled;
        layoutRequest.shadowOffsetXPixels = layer.shadowOffsetXPixels;
        layoutRequest.shadowSoftnessPixels = layer.shadowSoftnessPixels;
        zoneLayouts[index] = wipreview::text::layoutUTF8(layoutRequest);
        zoneGlyphs[index] = std::move(zoneLayouts[index].glyph);
        if (!zoneGlyphs[index].fillPixels.empty() &&
            layer.outlineRadiusPixels > 0) {
          outlineGenerationFailed = !wipreview::text::addOutline(
              zoneGlyphs[index], layer.outlineRadiusPixels) || outlineGenerationFailed;
        }
        if (!zoneGlyphs[index].fillPixels.empty() && layer.shadowEnabled) {
          shadowGenerationFailed = !wipreview::text::addShadow(
              zoneGlyphs[index], layer.shadowOffsetXPixels,
              layer.shadowOffsetDownPixels, layer.shadowSoftnessPixels) ||
              shadowGenerationFailed;
        }
      }
      if (!zoneGlyphs[index].shadowPixels.empty()) {
        auto shadowOverlay = layer.overlay;
        shadowOverlay.opacity = layer.shadowOpacity;
        for (int channel = 0; channel < 4; ++channel) {
          shadowOverlay.colour[channel] = layer.shadowColour[channel];
        }
        wipreview::probe::compositeTextMask(
            displayLinearView, requestedWindow,
            zoneGlyphs[index].shadowView(), shadowOverlay);
      }
      if (!zoneGlyphs[index].outlinePixels.empty()) {
        auto outlineOverlay = layer.overlay;
        outlineOverlay.opacity = layer.outlineOpacity;
        for (int channel = 0; channel < 4; ++channel) {
          outlineOverlay.colour[channel] = layer.outlineColour[channel];
        }
        wipreview::probe::compositeTextMask(
            displayLinearView, requestedWindow,
            zoneGlyphs[index].outlineView(), outlineOverlay);
      }
      wipreview::probe::compositeTextMask(
          displayLinearView, requestedWindow,
          zoneGlyphs[index].fillView(), layer.overlay);
      zoneOrigins[index] = wipreview::probe::computeTextOrigin(
          displayLinearView.bounds, zoneGlyphs[index].width, zoneGlyphs[index].height,
          layer.overlay);
      zoneRasterizationFailed = zoneRasterizationFailed ||
          (layer.overlay.enabled && !layer.text.empty() && zoneGlyphs[index].fillPixels.empty());
    }
    wipreview::probe::encodeManagedDisplayFrame(
        displayLinearView, outputView, requestedWindow, managedColor.config,
        options.outputPremultiplied);
    Logger::instance().write("MANAGED_COLOR",
        instancePrefix(instance) +
        " mode=" + std::to_string(managedColor.colorSpaceMode) +
        " host_colourspace=" + quoted(managedColor.hostColourspace.c_str()) +
        " host_recognized=" + (managedColor.hostRecognized ? "true" : "false") +
        " used_manual_interpretation=" +
            (managedColor.usedManualInterpretation ? "true" : "false") +
        " display_encoding=" + quoted(
            displayEncodingName(managedColor.config.encoding)) +
        " graphics_white_mode=" +
            std::to_string(managedColor.graphicsWhiteMode) +
        " graphics_white_nits=" +
            std::to_string(managedColor.config.graphicsWhiteNits) +
        " hlg_peak_nits=" + std::to_string(managedColor.config.peakNits) +
        " working_space=display-light-linear working_premult=true" +
        " encode_count=1 output_premult=" +
            (options.outputPremultiplied ? "true" : "false"));
    if (managedColor.colorSpaceMode == 0 && !managedColor.hostRecognized) {
      Logger::instance().write("RENDER_WARNING",
          instancePrefix(instance) +
          " unknown_host_colourspace=" +
              quoted(managedColor.hostColourspace.c_str()) +
          " manual_interpretation=" +
              quoted(displayEncodingName(managedColor.config.encoding)));
    }
    Logger::instance().write("STATIC_FORMATTER",
        instancePrefix(instance) +
        " placement=" + std::to_string(static_cast<int>(options.placement)) +
        " filter=" + std::to_string(static_cast<int>(options.filter)) +
        " source_PAR=" + std::to_string(options.sourcePixelAspect) +
        " output_PAR=" + std::to_string(options.outputPixelAspect) +
        " source_premult=" + (options.sourcePremultiplied ? "true" : "false") +
        " output_premult=" + (options.outputPremultiplied ? "true" : "false") +
        " canvas=[" + std::to_string(options.canvas[0]) + ',' +
                         std::to_string(options.canvas[1]) + ',' +
                         std::to_string(options.canvas[2]) + ',' +
                         std::to_string(options.canvas[3]) + ']');
    Logger::instance().write("EDITORIAL_BLANKING",
        instancePrefix(instance) +
        " enabled=" + (blanking.enabled ? "true" : "false") +
        " aspect=" + std::to_string(blanking.editorialAspect) +
        " output_PAR=" + std::to_string(blanking.outputPixelAspect) +
        " aperture=[" + std::to_string(aperture.x1) + ',' +
                          std::to_string(aperture.y1) + ',' +
                          std::to_string(aperture.x2) + ',' +
                          std::to_string(aperture.y2) + ']' +
        " colour=[" + std::to_string(blanking.colour[0]) + ',' +
                        std::to_string(blanking.colour[1]) + ',' +
                        std::to_string(blanking.colour[2]) + ',' +
                        std::to_string(blanking.colour[3]) + ']' +
        " opacity=" + std::to_string(blanking.opacity));
    Logger::instance().write("TEXT_OUTLINE",
        instancePrefix(instance) +
        " enabled=" + (globalText.outlineEnabled ? "true" : "false") +
        " normalized_width=" + std::to_string(globalText.normalizedOutlineWidth) +
        " pixel_radius=" + std::to_string(globalText.outlineRadiusPixels) +
        " colour=[" + std::to_string(globalText.outlineColour[0]) + ',' +
                       std::to_string(globalText.outlineColour[1]) + ',' +
                       std::to_string(globalText.outlineColour[2]) + ',' +
                       std::to_string(globalText.outlineColour[3]) + ']' +
        " opacity=" + std::to_string(globalText.outlineOpacity));
    Logger::instance().write("TEXT_SHADOW",
        instancePrefix(instance) +
        " enabled=" + (globalText.shadowEnabled ? "true" : "false") +
        " normalized_offset=[" + std::to_string(globalText.normalizedShadowOffsetX) + ',' +
                                  std::to_string(globalText.normalizedShadowOffsetY) + ']' +
        " pixel_offset=[" + std::to_string(globalText.shadowOffsetXPixels) + ',' +
                             std::to_string(globalText.shadowOffsetDownPixels) + ']' +
        " normalized_softness=" + std::to_string(globalText.normalizedShadowSoftness) +
        " pixel_softness=" + std::to_string(globalText.shadowSoftnessPixels) +
        " colour=[" + std::to_string(globalText.shadowColour[0]) + ',' +
                       std::to_string(globalText.shadowColour[1]) + ',' +
                       std::to_string(globalText.shadowColour[2]) + ',' +
                       std::to_string(globalText.shadowColour[3]) + ']' +
        " opacity=" + std::to_string(globalText.shadowOpacity));
    Logger::instance().write("TEXT_OVERFLOW",
        instancePrefix(instance) +
        " mode=" + std::to_string(static_cast<int>(globalText.overflowMode)) +
        " normalized_zone_gap=" + std::to_string(globalText.normalizedZoneGap) +
        " minimum_font_scale=" + std::to_string(globalText.minimumFontScale) +
        " minimum_policy=clip");
    Logger::instance().write("DYNAMIC_TEXT",
        instancePrefix(instance) +
        " time=" + std::to_string(time) +
        " fps_mode=" + std::to_string(dynamicText.fpsMode) +
        " host_fps=" + std::to_string(dynamicText.hostFps) +
        " selected_fps=" + std::to_string(dynamicText.resolver.fps) +
        " frame_relative_base=" +
            std::to_string(dynamicText.resolver.frameRelativeBase) +
        " frame_start=" + std::to_string(dynamicText.resolver.frameStart) +
        " timecode_start=" + quoted(dynamicText.resolver.timecodeStart.c_str()) +
        " drop_frame_mode=" + std::to_string(
            static_cast<int>(dynamicText.resolver.dropFrameMode)));
    for (std::size_t index = 0; index < zoneSettings.size(); ++index) {
      const auto& zone = zoneSettings[index];
      const auto& layer = zone.layer;
      const auto& zoneMask = zoneGlyphs[index];
      const auto& layout = zoneLayouts[index];
      const auto& token = zoneTokens[index];
      const auto& origin = zoneOrigins[index];
      Logger::instance().write("TEXT_ZONE",
          instancePrefix(instance) +
          " zone=" + quoted(kZoneParams[index].label) +
          " enabled=" + (layer.overlay.enabled ? "true" : "false") +
          " text=" + quoted(layer.text.c_str()) +
          " resolved_text=" + quoted(token.text.c_str()) +
          " rendered_text=" + quoted(layout.renderedText.c_str()) +
          " use_size_override=" + (zone.useSizeOverride ? "true" : "false") +
          " use_color_override=" + (zone.useColourOverride ? "true" : "false") +
          " use_opacity_override=" + (zone.useOpacityOverride ? "true" : "false") +
          " requested_font=" + quoted(layer.fontFamily.c_str()) +
          " resolved_font=" + quoted(zoneMask.resolvedFont.c_str()) +
          " fallback=" + (zoneMask.usedFallback ? "true" : "false") +
          " normalized_size=" + std::to_string(layer.normalizedSize) +
          " requested_pixel_size=" + std::to_string(layer.pixelSize) +
          " effective_pixel_size=" + std::to_string(layout.effectivePixelSize) +
          " effective_scale=" + std::to_string(layout.effectiveScale) +
          " overflowed=" + (layout.overflowed ? "true" : "false") +
          " clipped=" + (layout.clipped ? "true" : "false") +
          " ellipsized=" + (layout.ellipsized ? "true" : "false") +
          " cell=[" + std::to_string(layer.overlay.cellBounds.x1) + ',' +
                       std::to_string(layer.overlay.cellBounds.x2) + ']' +
          " mask=[" + std::to_string(zoneMask.width) + ',' +
                       std::to_string(zoneMask.height) + ']' +
          " outline=" + (!zoneMask.outlinePixels.empty() ? "true" : "false") +
          " shadow=" + (!zoneMask.shadowPixels.empty() ? "true" : "false") +
          " origin=[" + std::to_string(origin.x) + ',' + std::to_string(origin.y) + ']' +
          " offset=[" + std::to_string(layer.overlay.offsetX) + ',' +
                         std::to_string(layer.overlay.offsetY) + ']' +
          " colour=[" + std::to_string(layer.overlay.colour[0]) + ',' +
                          std::to_string(layer.overlay.colour[1]) + ',' +
                          std::to_string(layer.overlay.colour[2]) + ',' +
                          std::to_string(layer.overlay.colour[3]) + ']' +
          " opacity=" + std::to_string(layer.overlay.opacity));
      if (token.containsDynamicTokens) {
        Logger::instance().write("TOKEN_ZONE",
            instancePrefix(instance) +
            " zone=" + quoted(kZoneParams[index].label) +
            " source=" + quoted(layer.text.c_str()) +
            " resolved=" + quoted(token.text.c_str()) +
            " effect_frame=" + std::to_string(token.effectFrame) +
            " frame_rel=" + std::to_string(token.frameRelative) +
            " frame=" + std::to_string(token.frame) +
            " timecode=" + quoted(token.timecode.c_str()) +
            " nominal_fps=" + std::to_string(token.nominalFps) +
            " fps_valid=" + (token.fpsValid ? "true" : "false") +
            " drop_compatible=" + (token.dropCompatible ? "true" : "false") +
            " drop_applied=" + (token.dropApplied ? "true" : "false") +
            " timecode_start_valid=" +
                (token.timecodeStartValid ? "true" : "false") +
            " used_timecode_fallback=" +
                (token.usedTimecodeFallback ? "true" : "false"));
      }
    }
    if (outputView.pixelBytes == 0) {
      Logger::instance().write("RENDER_WARNING",
          instancePrefix(instance) + " unsupported_output_pixel_format=true");
      result = kOfxStatFailed;
    } else if (sourceImage && sourceView.pixelBytes == 0) {
      Logger::instance().write("RENDER_WARNING",
          instancePrefix(instance) + " unsupported_source_pixel_format=true output_canvas_only=true");
    } else if (zoneRasterizationFailed) {
      Logger::instance().write("RENDER_WARNING",
          instancePrefix(instance) + " zone_text_rasterization_failed=true output_continues=true");
    } else if (outlineGenerationFailed) {
      Logger::instance().write("RENDER_WARNING",
          instancePrefix(instance) + " glyph_outline_generation_failed=true");
    } else if (shadowGenerationFailed) {
      Logger::instance().write("RENDER_WARNING",
          instancePrefix(instance) + " glyph_shadow_generation_failed=true");
    } else if (timecodeResolutionFallback) {
      Logger::instance().write("RENDER_WARNING",
          instancePrefix(instance) + " timecode_resolution_fallback=true");
    } else if (options.placement == wipreview::probe::PlacementMode::Identity &&
               (sourceView.bounds.x2 - sourceView.bounds.x1 != outputView.bounds.x2 - outputView.bounds.x1 ||
                sourceView.bounds.y2 - sourceView.bounds.y1 != outputView.bounds.y2 - outputView.bounds.y1)) {
      Logger::instance().write("RENDER_WARNING",
          instancePrefix(instance) + " identity_raster_mismatch=true implicit_resize=false");
    }
  }

  if (sourceImage) gImageSuite->clipReleaseImage(sourceImage);
  if (outputImage) gImageSuite->clipReleaseImage(outputImage);
  return result;
}

OfxStatus instanceChanged(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs) {
  InstanceData* instance = getInstance(effect);
  if (!instance) return kOfxStatErrBadHandle;
  Logger::instance().write("INSTANCE_CHANGED",
      instancePrefix(instance) +
      " reason=" + getString(inArgs, kOfxPropChangeReason) +
      " type=" + getString(inArgs, kOfxPropType) +
      " name=" + getString(inArgs, kOfxPropName) +
      " time=" + getDouble(inArgs, kOfxPropTime) +
      " render_scale=" + joinDoubles(inArgs, kOfxImageEffectPropRenderScale));
  return kOfxStatReplyDefault;
}

OfxStatus pluginMainForProfile(DescriptorProfile profile, const char* action,
                               const void* handle, OfxPropertySetHandle inArgs,
                               OfxPropertySetHandle outArgs) noexcept {
  try {
    const auto effect = reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
    OfxStatus status = kOfxStatReplyDefault;
    if (std::strcmp(action, kOfxActionLoad) == 0) status = load();
    else if (std::strcmp(action, kOfxActionUnload) == 0) status = unload();
    else if (std::strcmp(action, kOfxActionDescribe) == 0) status = describe(effect, profile);
    else if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0)
      status = describeInContext(effect, inArgs);
    else if (std::strcmp(action, kOfxActionCreateInstance) == 0) status = createInstance(effect);
    else if (std::strcmp(action, kOfxActionDestroyInstance) == 0) status = destroyInstance(effect);
    else if (std::strcmp(action, kOfxImageEffectActionGetRegionOfDefinition) == 0)
      status = getRegionOfDefinition(effect, inArgs, outArgs);
    else if (std::strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0)
      status = getRegionsOfInterest(effect, inArgs, outArgs);
    else if (std::strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0)
      status = getClipPreferences(effect, outArgs);
    else if (std::strcmp(action, kOfxImageEffectActionGetOutputColourspace) == 0)
      status = getOutputColourspace(effect, inArgs, outArgs);
    else if (std::strcmp(action, kOfxImageEffectActionGetTimeDomain) == 0)
      status = getTimeDomain(effect, outArgs);
    else if (std::strcmp(action, kOfxImageEffectActionRender) == 0) status = render(effect, inArgs);
    else if (std::strcmp(action, kOfxActionInstanceChanged) == 0)
      status = instanceChanged(effect, inArgs);
    Logger::instance().write("ACTION",
        std::string("name=") + quoted(action) + " status=" + statusName(status));
    return status;
  } catch (const std::bad_alloc&) {
    Logger::instance().write("EXCEPTION", "type=bad_alloc");
    return kOfxStatErrMemory;
  } catch (const std::exception& error) {
    Logger::instance().write("EXCEPTION", "what=" + quoted(error.what()));
    return kOfxStatErrUnknown;
  } catch (...) {
    Logger::instance().write("EXCEPTION", "type=unknown");
    return kOfxStatErrUnknown;
  }
}

OfxStatus pluginMain(const char* action, const void* handle,
                     OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) noexcept {
  return pluginMainForProfile(DescriptorProfile::GeneralPreferred, action, handle,
                              inArgs, outArgs);
}

OfxStatus filterPluginMain(const char* action, const void* handle,
                           OfxPropertySetHandle inArgs,
                           OfxPropertySetHandle outArgs) noexcept {
  return pluginMainForProfile(DescriptorProfile::FilterOnly, action, handle,
                              inArgs, outArgs);
}

void setHost(OfxHost* host) {
  gHost = host;
}

OfxPlugin gPlugin = {
    kOfxImageEffectPluginApi,
    1,
    kPluginIdentifier,
    0,
    1,
    setHost,
    pluginMain,
};

OfxPlugin gFilterPlugin = {
    kOfxImageEffectPluginApi,
    1,
    kFilterPluginIdentifier,
    0,
    1,
    setHost,
    filterPluginMain,
};

}  // namespace

extern "C" WIPREVIEW_EXPORT OfxPlugin* OfxGetPlugin(int index) {
  if (index == 0) return &gPlugin;
  if (index == 1) return &gFilterPlugin;
  return nullptr;
}

extern "C" WIPREVIEW_EXPORT int OfxGetNumberOfPlugins() {
  return 2;
}

extern "C" WIPREVIEW_EXPORT OfxStatus OfxSetHost(const OfxHost* host) {
  gHost = const_cast<OfxHost*>(host);
  return kOfxStatOK;
}
