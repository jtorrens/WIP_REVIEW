#include "probe_core.hpp"

#include <ofxColour.h>
#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMessage.h>
#include <ofxParam.h>
#include <ofxProperty.h>

#include <algorithm>
#include <atomic>
#include <chrono>
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
constexpr char kNativeConfig[] = "ofx-native-v1.5_aces-v1.3_ocio-v2.3";
constexpr char kOutputClipPARPreference[] = "OfxImageClipPropPAR_Output";

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
  OfxImageEffectHandle effect = nullptr;
  OfxImageClipHandle source = nullptr;
  OfxImageClipHandle output = nullptr;
  OfxParamHandle requestRod = nullptr;
  OfxParamHandle width = nullptr;
  OfxParamHandle height = nullptr;
  OfxParamHandle scenario = nullptr;
  OfxParamHandle animatedString = nullptr;
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
      " premultiplication=" + getString(image, kOfxImageEffectPropPreMultiplication));
}

bool readRodRequest(const InstanceData* instance, bool& enabled, int& width, int& height) {
  if (!instance || !gParameterSuite) return false;
  int request = 1;
  width = 1920;
  height = 1080;
  const OfxStatus a = gParameterSuite->paramGetValue(instance->requestRod, &request);
  const OfxStatus b = gParameterSuite->paramGetValue(instance->width, &width);
  const OfxStatus c = gParameterSuite->paramGetValue(instance->height, &height);
  enabled = request != 0;
  width = std::max(1, width);
  height = std::max(1, height);
  return a == kOfxStatOK && b == kOfxStatOK && c == kOfxStatOK;
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

bool samePixelFormat(OfxPropertySetHandle a, OfxPropertySetHandle b) {
  char* aComponents = nullptr;
  char* bComponents = nullptr;
  char* aDepth = nullptr;
  char* bDepth = nullptr;
  return gPropertySuite->propGetString(a, kOfxImageEffectPropComponents, 0, &aComponents) == kOfxStatOK &&
         gPropertySuite->propGetString(b, kOfxImageEffectPropComponents, 0, &bComponents) == kOfxStatOK &&
         gPropertySuite->propGetString(a, kOfxImageEffectPropPixelDepth, 0, &aDepth) == kOfxStatOK &&
         gPropertySuite->propGetString(b, kOfxImageEffectPropPixelDepth, 0, &bDepth) == kOfxStatOK &&
         std::strcmp(aComponents, bComponents) == 0 && std::strcmp(aDepth, bDepth) == 0;
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
  }
  return view;
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
                               filterOnly ? "WIP Review Probe (P0 Filter Only)"
                                          : "WIP Review Probe (P0)");
  gPropertySuite->propSetString(properties, kOfxPropShortLabel, 0,
                               filterOnly ? "WIP Probe Filter" : "WIP Probe");
  gPropertySuite->propSetString(properties, kOfxPropLongLabel, 0,
                               filterOnly ? "WIP Review Host Capability Probe P0 — Filter Only"
                                          : "WIP Review Host Capability Probe P0");
  gPropertySuite->propSetString(properties, kOfxPropPluginDescription, 0,
      "Diagnostic-only P0 probe. Requests a custom output RoD and logs Resolve/Fusion host capabilities.");
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
                       const char* defaultValue, bool animates, const char* hint) {
  OfxPropertySetHandle properties = nullptr;
  if (gParameterSuite->paramDefine(params, kOfxParamTypeString, name, &properties) != kOfxStatOK) return;
  gPropertySuite->propSetString(properties, kOfxPropLabel, 0, label);
  gPropertySuite->propSetString(properties, kOfxParamPropDefault, 0, defaultValue);
  gPropertySuite->propSetString(properties, kOfxParamPropStringMode, 0, kOfxParamStringIsSingleLine);
  gPropertySuite->propSetInt(properties, kOfxParamPropAnimates, 0, animates ? 1 : 0);
  gPropertySuite->propSetString(properties, kOfxParamPropHint, 0, hint);
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
      "When enabled, GetRegionOfDefinition requests the width and height below.");

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
  gPropertySuite->propSetPointer(effectProperties, kOfxPropInstanceData, 0, instance);

  Logger::instance().write("INSTANCE_CREATE",
      instancePrefix(instance) + " log_path=" + quoted(Logger::instance().path().c_str()));
  logEffectProperties(instance, effectProperties);
  logClip(instance, "Source", instance->source, 0.0);
  logClip(instance, "Output", instance->output, 0.0);
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
  const OfxStatus outputPARStatus = gPropertySuite->propSetDouble(
      outArgs, kOutputClipPARPreference, 0, 1.0);
  gPropertySuite->propSetInt(outArgs, kOfxImageEffectFrameVarying, 0, 1);
  Logger::instance().write("GET_CLIP_PREFERENCES",
      instancePrefix(instance) + " source_components=" + quoted(components) +
      " source_depth=" + quoted(depth) +
      " source_premultiplication=" + quoted(premultiplication) +
      " requested_output_PAR=1 output_PAR_status=" + statusName(outputPARStatus) +
      " output_frame_varying=true negotiated_style=" +
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
    const bool formatsMatch = sourceImage && samePixelFormat(sourceImage, outputImage);
    const auto sourceView = formatsMatch ? imageView(sourceImage) : wipreview::probe::ImageView{};
    const auto outputView = imageView(outputImage);
    wipreview::probe::copyProbeFrame(
        sourceView, outputView,
        {renderWindow[0], renderWindow[1], renderWindow[2], renderWindow[3]});
    if (outputView.pixelBytes == 0) {
      Logger::instance().write("RENDER_WARNING",
          instancePrefix(instance) + " unsupported_output_pixel_format=true");
      result = kOfxStatFailed;
    } else if (sourceImage && !formatsMatch) {
      Logger::instance().write("RENDER_WARNING",
          instancePrefix(instance) + " source_output_format_mismatch=true output_cleared=true");
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
