#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/ConfigManager.hpp>

#include "Helpers.h"

inline HANDLE PHANDLE = nullptr;

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    PHANDLE = handle;

    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    if (COMPOSITOR_HASH != CLIENT_HASH) {
        notifyError(PHANDLE, "Failed to load, mismatched versions! (see logs)");
        throw efmt("version mismatch, built against {}, running compositor {}", CLIENT_HASH, COMPOSITOR_HASH);
    }

    Log::logger->log(Log::INFO, "[HyprTaliaMisc] Loading Plugin");

    HyprlandAPI::addDispatcherV2(PHANDLE, "taliamisc:hello", [](std::string args) {
        Log::logger->log(Log::INFO, "[HyprTaliaMisc] Hello from HyprTaliaMisc! Args: {}", args);
        HyprlandAPI::addNotification(
            PHANDLE,
            std::format("[HyprTaliaMisc] Hello! Args: {}", args),
            CHyprColor(0xFF00FF00),
            5000
        );
        return SDispatchResult{};
    });

    return {
        "HyprTaliaMisc",
        "Miscellaneous Hyprland utilities",
        "Talia-12",
        "0.0.1"
    };
}

APICALL EXPORT void PLUGIN_EXIT()
{
    Log::logger->log(Log::INFO, "[HyprTaliaMisc] Unloading Plugin");
}

APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}
