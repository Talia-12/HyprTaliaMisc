# HyprTaliaMisc

Hyprland plugin providing miscellaneous dispatchers (dwindleswap, cyclefloating, togglefloatingfocus).

## Reference

- Hyprland source (v0.54.2) is cloned at `~/Programming/hyprland/Hyprland` — read from there instead of the nix store.
- User's Hyprland keybinding config: `~/.dotfiles/home/tyrab/features/desktop/hyprland/bindings.nix`

## Commands

- `nix build` — build the plugin
- `just reload-plugin` — unload, reload, and re-enable fallthrough submaps

## Development notes

### Plugin reload caveat

`fallthroughSubmaps` is an in-memory set — it gets cleared when the plugin is unloaded/reloaded. After reloading the plugin, you must re-run your `taliamisc:setfallthrough` dispatchers to re-enable fallthrough on the desired submaps.

### Debug logging

Use `Log::logger->log(Log::INFO, "[HyprTaliaMisc] ...", ...)` with `std::format`-style placeholders. Valid log levels in the `Log::` namespace: `TRACE`, `DEBUG`, `INFO` (alias for DEBUG), `WARN`, `ERR`, `CRIT`.

Logs appear in the Hyprland session log file at `/run/user/1000/hypr/<instance>/hyprland.log`. Filter with:
```bash
grep 'HyprTaliaMisc' /run/user/1000/hypr/*/hyprland.log | tail -50
```
