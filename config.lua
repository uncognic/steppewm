-- steppewm config
local terminal = "foot"
local launcher = "wofi --show drun"
local file_manager = "thunar"
local screenshot = "grim -g \"$(slurp)\" - | tee ~/Screenshots/$(date +'%Y-%m-%d_%H-%M-%S').png | wl-copy"

-- battery_path = "/sys/class/power_supply/BAT0" -- leave empty to disable taskbar indicator
-- backlight_path = "/sys/class/backlight/amdgpu_bl1" -- leave empty to disable taskbar indicator and osd
-- xwayland = true -- xwayland-satellite required, off by default

-- scroll on taskbar indicators
brightness_scroll_up = "brightnessctl set 5%+"
brightness_scroll_down = "brightnessctl set 5%-"
volume_scroll_up = "pactl set-sink-volume @DEFAULT_SINK@ +5%"
volume_scroll_down = "pactl set-sink-volume @DEFAULT_SINK@ -5%"
volume_middle_button = "pactl set-sink-mute @DEFAULT_SINK@ toggle"

bind("Super", "Return", "spawn", terminal)
bind("Super", "d", "spawn", launcher)
bind("Super", "e", "spawn", file_manager)
bind("Super+Shift", "s", "spawn", screenshot)
bind("Super", "Escape", "quit")
bind("Alt", "Tab", "focus_next")
bind("Alt+Shift", "Tab", "focus_prev")
bind("Super", "Down", "minimize")
bind("Super", "Up", "maximize")
bind("Super", "q", "close")
bind("Super", "f", "fullscreen")
bind("Super+Shift", "c", "reload")
bind("Super", "Left", "snap_left")
bind("Super", "Right", "snap_right")
bind("Super+Shift", "Left", "snap_top_left")
bind("Super+Shift", "Right", "snap_top_right")
bind("Super", "p", "pin")
bindswitch("lid:on", "spawn", "systemctl suspend")

-- media
bind("", "XF86AudioMute", "spawn", "pactl set-sink-mute @DEFAULT_SINK@ toggle")
bind("", "XF86AudioLowerVolume", "spawn", "pactl set-sink-volume @DEFAULT_SINK@ -5%")
bind("", "XF86AudioRaiseVolume", "spawn", "pactl set-sink-volume @DEFAULT_SINK@ +5%")
bind("", "XF86AudioMicMute", "spawn", "pactl set-source-mute @DEFAULT_SOURCE@ toggle")
bind("", "XF86MonBrightnessDown", "spawn", "brightnessctl set 5%-")
bind("", "XF86MonBrightnessUp", "spawn", "brightnessctl set 5%+")
bind("", "XF86AudioPlay", "spawn", "playerctl play-pause")
bind("", "XF86AudioPause", "spawn", "playerctl pause")
bind("", "XF86AudioNext", "spawn", "playerctl next")
bind("", "XF86AudioPrev", "spawn", "playerctl previous")

for i = 1, 9 do
    bind("Super", tostring(i), "workspace", tostring(i))
    bind("Super+Shift", tostring(i), "move_to_workspace", tostring(i))
end

-- exec("swayidle -w timeout 60 'wlopm --off \\*' resume 'wlopm --on \\*' timeout 63 'swaylock -f -c 000000' before-sleep 'swaylock -f -c 000000'")
-- exec("swaybg -i /path/to/wallpaper -m fill")
-- exec("nm-applet")
-- exec('blueman-applet')

-- exec("firefox")

-- outputs, names match wlr output names (DP-1, HDMI-A-1, eDP-1)
-- all fields are optional
-- omit position for auto-placement
-- output("eDP-1", {
--     mode = "1920x1200@144Hz",-- "WxH" or "WxH@Hz"
--     position = { 0, 0 },     -- layout coordinates
--     scale = 1.0,             -- scaling
--     transform = "normal",    -- "normal", "90", "180", "270", "flipped", "flipped-90", "flipped-180", "flipped-270"
-- })
-- output("HDMI-A-1", {
--     mode = "1920x1080",
--     position = { 1920, 0 },
--     transform = "normal"
-- })

-- keyboard (xkb)
-- keyboard_layout = "us,ru,ru,ca"        -- one entry per group
-- keyboard_variant = ",,phonetic,multix" -- positional: us=, ru=, ru=phonetic, ca=multix
-- keyboard_options = "grp:alt_shift_toggle"
-- repeat_rate = 25
-- repeat_delay = 600

-- input: pointer / touchpad (libinput)
-- tap_to_click = true
-- natural_scroll = false
-- pointer_accel = 0.0                    -- -1.0 (slow) .. 1.0 (fast)
-- accel_profile = "flat"                 -- "flat" or "adaptive"

-- theme = "icegil-korstro-Large"

-- font = "sans-serif"
-- title_font_size = 0                            -- 0 = auto (55% of title height)
-- title_height = 20
-- border_width = 3
-- corner_size = 8
-- close_button_width = 40
-- minimize_button_width = 20

-- pinned apps
-- pin("app_id", "launch command")
-- pin("app_id", "launch command", "/path/to/icon.png") -- optional icon override

-- pin("thunar", "thunar")
-- pin("firefox", "firefox")
-- pin("org.gnome.Nautilus", "nautilus")
-- pin("foot", "foot")
-- pin("org.signal.Signal", "flatpak run org.signal.Signal") -- signal's app_id for some reason is the full one but yeah
-- pin("spotify", "flatpak run com.spotify.Client")

-- taskbar
-- taskbar_all_outputs = true
-- taskbar_all_workspaces = true
-- taskbar_button_width = 200
-- taskbar_button_pad = 2
-- taskbar_height = 24
-- taskbar_bg = {0.08, 0.08, 0.08, 1.0}
-- taskbar_accent = {0.3, 0.5, 0.9, 1.0}         -- 1px accent line at top (alpha=0 = none)
-- task_normal = {0.18, 0.18, 0.18, 1.0}
-- task_active = {0.30, 0.30, 0.30, 1.0}
-- task_minimized = {0.12, 0.12, 0.12, 1.0}
-- task_urgent = {0.65, 0.08, 0.08, 1.0}
-- task_text = {0.88, 0.88, 0.88, 1.0}

