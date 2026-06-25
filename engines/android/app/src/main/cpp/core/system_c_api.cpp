#include "system_c_api.h"

#include "../engine/engine_config.h"

namespace {

constexpr const char* kCapabilitiesJson =
        "{"
        "\"abiVersion\":\"0.1\","
        "\"library\":\"libengine.so\","
        "\"core\":\"SystemApi\","
        "\"platform\":\"android\","
        "\"scriptBindings\":[\"lua\",\"js-reserved\",\"plugin-reserved\"],"
        "\"automationModes\":[\"root\",\"accessibility\"],"
        "\"rootCommandMode\":\"persistent-su-shell\","
        "\"screenCapture\":[\"root-surface\",\"root-screen-capture\",\"media-projection\"],"
        "\"imageHandle\":\"native-memory\""
        "}";

} // namespace

extern "C" const char* ael_system_version() {
    return EngineConfig::kEngineVersion;
}

extern "C" const char* ael_system_capabilities_json() {
    return kCapabilitiesJson;
}
