#pragma once

#include <string>
#include <format>
#include <stdexcept>

#include <hyprland/src/plugins/PluginAPI.hpp>

template <typename... Args>
inline auto efmt(std::format_string<Args...> fmt, Args&&... args)
{
    return std::runtime_error(std::format(fmt, std::forward<Args>(args)...));
}

inline void notifyError(HANDLE handle, const std::string& err)
{
    std::string msg = std::string("[HyprTaliaMisc] ") + err;
    Log::logger->log(Log::ERR, msg);
    HyprlandAPI::addNotification(
        handle,
        msg,
        CHyprColor(0xFFFF0000),
        25'000
    );
}
