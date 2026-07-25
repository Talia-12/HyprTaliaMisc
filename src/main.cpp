#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/math/Direction.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>

// Include the algorithm headers normally first so their transitive
// standard library includes aren't affected by the private→public hack.
#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>
#include <hyprland/src/layout/algorithm/tiled/dwindle/DwindleAlgorithm.hpp>

// KeybindManager.hpp is transitively included by ConfigManager.hpp, so it
// must appear inside the private→public block BEFORE ConfigManager.
#define private public
#include <hyprland/src/managers/KeybindManager.hpp>
#undef private

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/Compositor.hpp>

#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>

#include "Helpers.h"

inline HANDLE PHANDLE = nullptr;

// --- Submap fallthrough ---
static CHyprSignalListener                keyListener;
static std::unordered_set<std::string>    fallthroughSubmaps;
static SP<SHyprCtlCommand>               getfallthroughCmd;

static bool submapHasMatchingBind(const std::string& submap, uint32_t keycode, uint32_t mods) {
    const uint32_t XKBKEYCODE = keycode + 8; // evdev → XKB offset

    for (auto& kb : g_pKeybindManager->m_keybinds) {
        if (kb->submap.name != submap)
            continue;
        if (kb->catchAll || kb->mouse || kb->release)
            continue;
        if (kb->modmask != mods && !kb->ignoreMods)
            continue;

        // Match by keycode if the keybind specifies one
        if (kb->keycode != 0) {
            if (kb->keycode == XKBKEYCODE)
                return true;
            continue;
        }

        // Match by key name → resolve to keysym and compare against event keysym
        if (!kb->key.empty() && g_pKeybindManager->m_xkbTranslationState) {
            const xkb_keysym_t eventKeysym = xkb_state_key_get_one_sym(g_pKeybindManager->m_xkbTranslationState, XKBKEYCODE);
            if (eventKeysym == XKB_KEY_NoSymbol)
                continue;
            const auto KBKEY      = xkb_keysym_from_name(kb->key.c_str(), XKB_KEYSYM_NO_FLAGS);
            const auto KBKEYLOWER = xkb_keysym_from_name(kb->key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
            if (eventKeysym == KBKEY || eventKeysym == KBKEYLOWER)
                return true;
        }
    }
    return false;
}

// Mirrors the internal xkb_keysym_is_modifier() which is not part of
// libxkbcommon's public API.  Keeps modifier-only key presses from
// triggering submap fallthrough.
static bool isModifierKeysym(xkb_keysym_t sym) {
    return (sym >= XKB_KEY_Shift_L    && sym <= XKB_KEY_Hyper_R)          ||
           (sym >= XKB_KEY_ISO_Lock   && sym <= XKB_KEY_ISO_Level5_Lock)  ||
           sym == XKB_KEY_Mode_switch                                     ||
           sym == XKB_KEY_Num_Lock;
}

static void onKeyEvent(IKeyboard::SKeyEvent event, Event::SCallbackInfo&) {
    if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    auto& current = Config::Actions::state()->m_currentSubmap;
    if (current.empty())
        return;
    if (!fallthroughSubmaps.contains(current))
        return;

    // Ignore bare modifier presses — they shouldn't trigger fallthrough
    if (g_pKeybindManager->m_xkbTranslationState) {
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(
            g_pKeybindManager->m_xkbTranslationState, event.keycode + 8);
        if (isModifierKeysym(sym))
            return;
    }

    const uint32_t mods = g_pInputManager->getModsFromAllKBs();

    if (!submapHasMatchingBind(current, event.keycode, mods)) {
        current.clear(); // Fall through to main map
        g_pEventManager->postEvent(SHyprIPCEvent{"submap", ""});
        Event::bus()->m_events.keybinds.submap.emit(std::string{""});
    }
}

static Math::eDirection parseDirection(const std::string& arg) {
    if (arg == "l" || arg == "left")  return Math::DIRECTION_LEFT;
    if (arg == "r" || arg == "right") return Math::DIRECTION_RIGHT;
    if (arg == "u" || arg == "up")    return Math::DIRECTION_UP;
    if (arg == "d" || arg == "down")  return Math::DIRECTION_DOWN;
    return Math::DIRECTION_DEFAULT;
}

static bool isDescendantOf(SP<Layout::Tiled::SDwindleNodeData> node, SP<Layout::Tiled::SDwindleNodeData> ancestor) {
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

    auto dir = parseDirection(dirStr);
    if (dir == Math::DIRECTION_DEFAULT)
        return {.success = false, .error = "Invalid direction: " + dirStr};

    PHLWINDOW inputWindow;
    if (windowArg.empty())
        inputWindow = Desktop::focusState()->window();
    else
        inputWindow = Desktop::viewState()->query().selector(windowArg).runWindow();

    if (!inputWindow)
        return {.success = false, .error = "No input window found"};

    auto workspace = inputWindow->m_workspace;
    if (!workspace || !workspace->m_space || !workspace->m_space->algorithm())
        return {.success = false, .error = "No layout space for workspace"};

    auto* dwindleAlgo = dynamic_cast<Layout::Tiled::CDwindleAlgorithm*>(workspace->m_space->algorithm()->tiledAlgo().get());
    if (!dwindleAlgo) {
        notifyError(PHANDLE, "dwindleswap requires the Dwindle layout");
        return {};
    }

    PHLWINDOW dirWindow = Desktop::windowState()->query().inDirection(inputWindow, dir);
    if (!dirWindow)
        return {};

    auto inputNode = dwindleAlgo->getNodeFromWindow(inputWindow);
    auto dirNode   = dwindleAlgo->getNodeFromWindow(dirWindow);
    if (!inputNode || !dirNode)
        return {};

    // Walk up from inputNode's parent, find the first ancestor that contains dirNode
    for (auto parent = inputNode->pParent.lock(); parent; parent = parent->pParent.lock()) {
        if (isDescendantOf(dirNode, parent)) {
            std::swap(parent->children[0], parent->children[1]);
            workspace->m_space->recalculate();
            return {};
        }
    }

    return {};
}

static SDispatchResult cycleFloating(std::string args) {
    auto dir = parseDirection(args);
    if (dir == Math::DIRECTION_DEFAULT)
        return {.success = false, .error = "Invalid direction: " + args};

    bool increasing = (dir == Math::DIRECTION_RIGHT || dir == Math::DIRECTION_DOWN);

    // Collect visible floating windows
    std::vector<PHLWINDOW> floating;
    for (const auto& w : Desktop::windowState()->windows()) {
        if (w->m_isFloating && w->m_isMapped && !w->isHidden() &&
            w->m_workspace && w->m_workspace->isVisible())
            floating.push_back(w);
    }

    if (floating.empty()) {
        notifyError(PHANDLE, "No visible floating windows");
        return {};
    }

    // Find the target window to focus
    PHLWINDOW target;
    auto      active = Desktop::focusState()->window();
    auto      it     = std::find(floating.begin(), floating.end(), active);

    if (it != floating.end()) {
        // Active window is floating — cycle from its position
        size_t i = it - floating.begin();
        size_t n = floating.size();
        target = floating[increasing ? (i + 1) % n : (i + n - 1) % n];
    } else {
        // Active window is not floating — enter the list from the appropriate end
        target = increasing ? floating.front() : floating.back();
    }

    g_pInputManager->unconstrainMouse();
    Desktop::focusState()->fullWindowFocus(target, Desktop::FOCUS_REASON_OTHER);
    target->warpCursor();

    return {};
}

static SDispatchResult toggleFloatingFocus(std::string) {
    auto active = Desktop::focusState()->window();
    if (!active)
        return {};

    PHLWINDOW target;

    if (active->m_isFloating) {
        // Focus the tiled window beneath the centre of this floating window
        auto center = active->getWindowMainSurfaceBox().middle();
        target = Desktop::viewState()->hitTest().windowAt(center, Desktop::View::WINDOW_ONLY, active);
    } else {
        // Focus the first visible floating window that overlaps this tiled window
        auto activeBox = active->getWindowMainSurfaceBox();
        for (const auto& w : Desktop::windowState()->windows()) {
            if (w->m_isFloating && w->m_isMapped && !w->isHidden() &&
                w->m_workspace && w->m_workspace->isVisible() &&
                w->getWindowMainSurfaceBox().overlaps(activeBox)) {
                target = w;
                break;
            }
        }
    }

    if (!target)
        return {};

    g_pInputManager->unconstrainMouse();
    Desktop::focusState()->fullWindowFocus(target, Desktop::FOCUS_REASON_OTHER);
    target->warpCursor();

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

    HyprlandAPI::addDispatcherV2(PHANDLE, "taliamisc:cyclefloating", [](std::string args) {
        return cycleFloating(args);
    });

    HyprlandAPI::addDispatcherV2(PHANDLE, "taliamisc:togglefloatingfocus", [](std::string args) {
        return toggleFloatingFocus(args);
    });

    // --- Submap fallthrough dispatchers ---
    HyprlandAPI::addDispatcherV2(PHANDLE, "taliamisc:setfallthrough", [](std::string args) {
        auto spacePos = args.rfind(' ');
        if (spacePos == std::string::npos)
            return SDispatchResult{.success = false, .error = "Expected: <submap> <true|false>"};
        auto name = args.substr(0, spacePos);
        auto val  = args.substr(spacePos + 1);
        if (val == "true" || val == "1")
            fallthroughSubmaps.insert(name);
        else
            fallthroughSubmaps.erase(name);
        return SDispatchResult{};
    });

    HyprlandAPI::addDispatcherV2(PHANDLE, "taliamisc:togglefallthrough", [](std::string args) {
        if (fallthroughSubmaps.contains(args))
            fallthroughSubmaps.erase(args);
        else
            fallthroughSubmaps.insert(args);
        return SDispatchResult{};
    });

    getfallthroughCmd = HyprlandAPI::registerHyprCtlCommand(PHANDLE, SHyprCtlCommand{
        .name = "taliamisc:getfallthrough",
        .exact = true,
        .fn = [](eHyprCtlOutputFormat format, std::string args) -> std::string {
            // Trim leading/trailing whitespace
            while (!args.empty() && args.front() == ' ') args.erase(args.begin());
            while (!args.empty() && args.back() == ' ') args.pop_back();
            bool enabled = fallthroughSubmaps.contains(args);
            if (format == eHyprCtlOutputFormat::FORMAT_JSON)
                return std::format(R"({{"submap":"{}","fallthrough":{}}})", args, enabled ? "true" : "false");
            return enabled ? "true" : "false";
        }
    });

    // Register keyboard event hook for submap fallthrough
    keyListener = Event::bus()->m_events.input.keyboard.key.listen(onKeyEvent);

    return {
        "HyprTaliaMisc",
        "Miscellaneous Hyprland utilities",
        "Talia-12",
        "0.0.1"
    };
}

APICALL EXPORT void PLUGIN_EXIT()
{
    keyListener.reset();
    getfallthroughCmd.reset();
    fallthroughSubmaps.clear();
    Log::logger->log(Log::INFO, "[HyprTaliaMisc] Unloading Plugin");
}

APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}
