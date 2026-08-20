#include <ofxCore.h>
#include <ofxImageEffect.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstring>
#include <iostream>

namespace {

using GetPluginCount = int (*)();
using GetPlugin = OfxPlugin* (*)(int);

#if defined(_WIN32)
using LibraryHandle = HMODULE;

LibraryHandle openLibrary(const char* path) { return LoadLibraryA(path); }

void* loadSymbol(LibraryHandle library, const char* name) {
  return reinterpret_cast<void*>(GetProcAddress(library, name));
}

void closeLibrary(LibraryHandle library) { FreeLibrary(library); }
#else
using LibraryHandle = void*;

LibraryHandle openLibrary(const char* path) {
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void* loadSymbol(LibraryHandle library, const char* name) {
  return dlsym(library, name);
}

void closeLibrary(LibraryHandle library) { dlclose(library); }
#endif

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return fail("expected path to OFX binary");
  LibraryHandle library = openLibrary(argv[1]);
  if (!library) {
#if defined(_WIN32)
    std::cerr << "LoadLibrary failed: " << GetLastError() << '\n';
#else
    std::cerr << "dlopen failed: " << dlerror() << '\n';
#endif
    return 1;
  }

  auto getCount = reinterpret_cast<GetPluginCount>(
      loadSymbol(library, "OfxGetNumberOfPlugins"));
  auto getPlugin = reinterpret_cast<GetPlugin>(
      loadSymbol(library, "OfxGetPlugin"));
  if (!getCount || !getPlugin) {
    closeLibrary(library);
    return fail("mandatory OpenFX exports are missing");
  }
  if (getCount() != 1) {
    closeLibrary(library);
    return fail("bundle must expose exactly one WIP Review effect");
  }
  OfxPlugin* plugin = getPlugin(0);
  if (!plugin || std::strcmp(plugin->pluginApi, kOfxImageEffectPluginApi) != 0 ||
      plugin->apiVersion != 1 || !plugin->setHost || !plugin->mainEntry ||
      std::strcmp(plugin->pluginIdentifier,
                  "com.jtorrens.WIPReviewProbe") != 0) {
    closeLibrary(library);
    return fail("plugin metadata or entry points are invalid");
  }
  if (getPlugin(1) != nullptr) {
    closeLibrary(library);
    return fail("out-of-range plugin lookup should return null");
  }
  closeLibrary(library);
  return 0;
}
