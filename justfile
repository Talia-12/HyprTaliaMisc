set shell := ["nu", "-c"]

conf := "~/.config/hypr/hyprland.conf"

# Reload the plugin: unload, load, and re-enable fallthrough submaps
reload-plugin:
    #!/usr/bin/env nu
    let conf = open (echo {{conf}} | path expand) | lines

    # Find the plugin load line and extract the path
    let plugin_line = ($conf | where { $in =~ 'hyprctl plugin load' and $in =~ 'HyprTaliaMisc' } | first)
    let conf_path = ($plugin_line | parse --regex 'hyprctl plugin load (?P<path>.+)' | get path.0 | str trim)
    let conf_realpath = ($conf_path | path expand)

    let fallback_path = ("result/lib/libHyprTaliaMisc.so" | path expand)

    # Try unloading from config path, then fallback path
    mut unloaded = false
    if (do { hyprctl plugin unload $conf_realpath } | complete).exit_code == 0 {
        $unloaded = true
        print $"Unloaded ($conf_realpath)"
    } else if (do { hyprctl plugin unload $fallback_path } | complete).exit_code == 0 {
        $unloaded = true
        print $"Unloaded ($fallback_path)"
    }

    if not $unloaded {
        print -e $"Failed to unload plugin from either ($conf_realpath) or ($fallback_path)"
        exit 1
    }

    # Load the plugin
    hyprctl plugin load $conf_realpath
    print $"Loaded ($conf_realpath)"

    # Re-run setfallthrough commands from exec-once lines
    let fallthrough_cmds = ($conf
        | where { $in =~ 'exec-once=taliamisc:setfallthrough' }
        | each { $in | parse --regex 'exec-once=(?P<cmd>.+)' | get cmd.0 | str trim }
    )

    for cmd in $fallthrough_cmds {
        print $"Running: ($cmd)"
        nu -c $cmd
    }
