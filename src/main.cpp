#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>

// Include IHyprLayout and DesktopTypes normally first so their transitive
// standard library includes aren't affected by the private→public hack.
#include <hyprland/src/layout/IHyprLayout.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#define private public
#include <hyprland/src/layout/DwindleLayout.hpp>
#undef private

#include <hyprland/src/managers/LayoutManager.hpp>

#include "Helpers.h"

inline HANDLE PHANDLE = nullptr;

static char parseDirection(const std::string& arg) {
    if (arg == "l" || arg == "left")  return 'l';
    if (arg == "r" || arg == "right") return 'r';
    if (arg == "u" || arg == "up")    return 'u';
    if (arg == "d" || arg == "down")  return 'd';
    return 0;
}

static bool isDescendantOf(SP<SDwindleNodeData> node, SP<SDwindleNodeData> ancestor) {
    while (node) {
        if (node == ancestor)
            return true;
        node = node->pParent.lock();
    }
    return false;
}

static SDispatchResult dwindleSwapDirection(std::string args) {
    // Parse: "<direction> [window_specifier]"
    std::string dirStr, windowArg;
    auto spacePos = args.find(' ');
    if (spacePos != std::string::npos) {
        dirStr    = args.substr(0, spacePos);
        windowArg = args.substr(spacePos + 1);
    } else {
        dirStr = args;
    }

    char dir = parseDirection(dirStr);
    if (!dir)
        return {.success = false, .error = "Invalid direction: " + dirStr};

    PHLWINDOW inputWindow;
    if (windowArg.empty())
        inputWindow = Desktop::focusState()->window();
    else
        inputWindow = g_pCompositor->getWindowByRegex(windowArg);

    if (!inputWindow)
        return {.success = false, .error = "No input window found"};

    auto* dwindleLayout = dynamic_cast<CHyprDwindleLayout*>(g_pLayoutManager->getCurrentLayout());
    if (!dwindleLayout) {
        notifyError(PHANDLE, "dwindleswap requires the Dwindle layout");
        return {};
    }

    PHLWINDOW dirWindow = g_pCompositor->getWindowInDirection(inputWindow, dir);
    if (!dirWindow)
        return {};

    auto inputNode = dwindleLayout->getNodeFromWindow(inputWindow);
    auto dirNode   = dwindleLayout->getNodeFromWindow(dirWindow);
    if (!inputNode || !dirNode)
        return {};

    // Walk up from inputNode's parent, find the first ancestor that contains dirNode
    for (auto parent = inputNode->pParent.lock(); parent; parent = parent->pParent.lock()) {
        if (isDescendantOf(dirNode, parent)) {
            std::swap(parent->children[0], parent->children[1]);
            parent->recalcSizePosRecursive();
            return {};
        }
    }

    return {};
}

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

    HyprlandAPI::addDispatcherV2(PHANDLE, "taliamisc:dwindleswap", [](std::string args) {
        return dwindleSwapDirection(args);
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
