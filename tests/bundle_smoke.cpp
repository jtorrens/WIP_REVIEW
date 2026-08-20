#include <ofxCore.h>
#include <ofxImageEffect.h>

#include <dlfcn.h>

#include <cstring>
#include <iostream>

namespace {

using GetPluginCount = int (*)();
using GetPlugin = OfxPlugin* (*)(int);

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return fail("expected path to OFX binary");
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    std::cerr << "dlopen failed: " << dlerror() << '\n';
    return 1;
  }

  auto getCount = reinterpret_cast<GetPluginCount>(dlsym(library, "OfxGetNumberOfPlugins"));
  auto getPlugin = reinterpret_cast<GetPlugin>(dlsym(library, "OfxGetPlugin"));
  if (!getCount || !getPlugin) {
    dlclose(library);
    return fail("mandatory OpenFX exports are missing");
  }
  if (getCount() != 2) {
    dlclose(library);
    return fail("bundle must expose the general-preferred and filter-only effects");
  }
  const char* expectedIdentifiers[] = {
      "com.jtorrens.WIPReviewProbe",
      "com.jtorrens.WIPReviewProbe.Filter",
  };
  for (int index = 0; index < 2; ++index) {
    OfxPlugin* plugin = getPlugin(index);
    if (!plugin || std::strcmp(plugin->pluginApi, kOfxImageEffectPluginApi) != 0 ||
        plugin->apiVersion != 1 || !plugin->setHost || !plugin->mainEntry ||
        std::strcmp(plugin->pluginIdentifier, expectedIdentifiers[index]) != 0) {
      dlclose(library);
      return fail("plugin metadata or entry points are invalid");
    }
  }
  if (getPlugin(2) != nullptr) {
    dlclose(library);
    return fail("out-of-range plugin lookup should return null");
  }
  dlclose(library);
  return 0;
}
