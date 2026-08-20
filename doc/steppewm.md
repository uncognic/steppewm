# steppewm configuration reference

## bind(modifiers, key, action [, arg])

Bind a key combination to an action. Up to 128 bindings.

Modifiers are `+`-separated and case-insensitive: `"Alt"`, `"Super"` (or
`"Mod4"`), `"Shift"`, `"Ctrl"` (or `"Control"`). Use `""` for no modifier.
Key names are xkb keysym names (case-insensitive), e.g. `"Return"`, `"Tab"`,
`"q"`, `"Left"`, `"XF86AudioMute"`.

```lua
bind("Super", "Return", "spawn", "foot")
bind("Super", "q", "close")
bind("Alt", "Tab", "focus_next")
bind("", "XF86AudioMute", "spawn", "pactl set-sink-mute @DEFAULT_SINK@ toggle")

for i = 1, 9 do
    bind("Super", tostring(i), "workspace", tostring(i))
    bind("Super+Shift", tostring(i), "move_to_workspace", tostring(i))
end
```

Available actions:

- `spawn <command>` - run a shell command
- `quit` - exit the compositor
- `reload` - reload configuration
- `focus_next` - focus next window
- `focus_prev` - focus previous window
- `close` - close focused window
- `minimize` - minimize focused window
- `maximize` - toggle maximize
- `fullscreen` - toggle fullscreen
- `pin` - toggle pin (sticky across workspaces)
- `snap_left`, `snap_right` - snap to left/right half
- `snap_top_left`, `snap_top_right`, `snap_bottom_left`, `snap_bottom_right` - snap to quarter
- `workspace <"1"-"9">` - switch to workspace
- `move_to_workspace <"1"-"9">` - move focused window to workspace

## bindswitch(switch, action [, arg])

Bind a hardware switch event. Up to 16 bindings. Format is `"type:state"` -
type is `lid` or `tablet`, state is `on` or `off`.

```lua
bindswitch("lid:on", "spawn", "systemctl suspend")
```

## exec(command)

Run a shell command at startup. These are tracked and not re-run on config
reload. Up to 64 commands.

```lua
exec("swaybg -i ~/wallpaper.png -m fill")
exec("nm-applet")
```

## output(name, options)

Configure a display output. Up to 16 outputs. Names match wlr output names
(`DP-1`, `HDMI-A-1`, `eDP-1`). All fields are optional. Omit `position` for
auto-placement.

```lua
output("eDP-1", {
    mode = "1920x1200@144Hz",  -- "WxH" or "WxH@Hz"
    position = { 0, 0 },
    scale = 1.0,
    transform = "normal",     -- "normal", "90", "180", "270",
                               -- "flipped", "flipped-90", "flipped-180", "flipped-270"
    enabled = true,
})
output("HDMI-A-1", {
    mode = "1920x1080",
    position = { 1920, 0 },
})
```

## pin(app_id, command [, icon_path])

Pin an application to the taskbar. Up to 32 pins. The `app_id` is the Wayland
app_id used to match running windows. The optional third argument overrides the
icon with a PNG path.

```lua
pin("thunar", "thunar")
pin("firefox", "firefox")
pin("foot", "foot", "/path/to/custom-icon.png")
```

## General

`xwayland` (bool, default `false`) - enable XWayland support. Requires
xwayland-satellite.

`theme` (string, default `""`) - theme directory name. See [Themes](#themes).

## Keyboard

Keyboard settings use XKB. Layouts and variants are comma-separated - the
variant list is positional, matching each layout in order.

```lua
keyboard_layout = "us,ru"
keyboard_variant = ",phonetic"
keyboard_options = "grp:alt_shift_toggle"
repeat_rate = 25    -- keys/sec (default 25)
repeat_delay = 600  -- ms (default 600)
```

## Input

Pointer and touchpad settings are passed through to libinput.

```lua
tap_to_click = true       -- default false
natural_scroll = false    -- default false
pointer_accel = 0.0       -- -1.0 (slow) to 1.0 (fast), default 0.0
accel_profile = "flat"    -- "flat" or "adaptive", default is libinput's
```

## Window decorations

### Geometry

```lua
font = "sans-serif"
title_font_size = 0       -- 0 = auto 
title_height = 20         -- px
border_width = 3          -- px
corner_size = 8           -- corner resize hit area, px
close_button_width = 40   -- px
maximize_button_width = 20
minimize_button_width = 20
```

### Style

`button_style` (string, default `"symbol"`) - `"symbol"` draws a rectangle
with an icon inside, `"circle"` draws circular buttons with the icon shown on
hover.

`border_style` (string, default `"flat"`) - `"flat"` is a solid color,
`"bevel"` adds light/dark edges.

`title_gradient` (bool, default `false`) - use a vertical gradient on the
titlebar instead of a flat fill.

`center_title_text` (bool, default `false`) - center the title text instead of
left-aligning it.

`buttons_left` (bool, default `false`) - place window buttons on the left side.

### Colors

Colors are `{R, G, B, A}` tables with float values 0.0–1.0.

```lua
title_active = {0.24, 0.24, 0.24, 1.0}
title_inactive = {0.14, 0.14, 0.14, 1.0}
title_text = {0.88, 0.88, 0.88, 1.0}
```

`border_color` is the base border color.
`border_active` and `border_inactive` inherit from it if not set.

```lua
border_color = {0.20, 0.20, 0.20, 1.0}
border_active = {0.30, 0.30, 0.50, 1.0}   -- override for focused windows
border_inactive = {0.15, 0.15, 0.15, 1.0} -- override for unfocused
```

Button colors work similarly - `button_color` and `button_inactive` are the
base for maximize/minimize, and each can be overridden individually:

```lua
close_active = {0.85, 0.08, 0.08, 1.0}
close_inactive = {0.45, 0.06, 0.06, 1.0}
close_hover = {0, 0, 0, 0}                -- alpha=0 means auto-lighten

button_color = {0.38, 0.38, 0.38, 1.0}    -- base for max/min (focused)
button_inactive = {0.32, 0.32, 0.32, 1.0} -- base for max/min (unfocused)

maximize_active = {0.38, 0.38, 0.38, 1.0} -- overrides button_color
maximize_inactive = {0.32, 0.32, 0.32, 1.0}
maximize_hover = {0, 0, 0, 0}

minimize_active = {0.38, 0.38, 0.38, 1.0}
minimize_inactive = {0.32, 0.32, 0.32, 1.0}
minimize_hover = {0, 0, 0, 0}
```

All hover colors default to `{0, 0, 0, 0}` (alpha = 0), which auto-lightens
the active color on hover.

## Taskbar

```lua
taskbar_height = 24          -- px
taskbar_button_width = 150   -- max button width, px
taskbar_button_pad = 2       -- padding between buttons, px
taskbar_all_outputs = false  -- show on all outputs
```

Taskbar colors:

```lua
taskbar_bg = {0.08, 0.08, 0.08, 1.0}
taskbar_accent = {0.3, 0.5, 0.9, 1.0}    -- 1px line at top, alpha=0 disables
task_normal = {0.18, 0.18, 0.18, 1.0}
task_active = {0.30, 0.30, 0.30, 1.0}
task_minimized = {0.12, 0.12, 0.12, 1.0}
task_urgent = {0.65, 0.08, 0.08, 1.0}
task_text = {0.88, 0.88, 0.88, 1.0}
tray_bg = {0.18, 0.18, 0.18, 1.0}        -- inherits task_normal if not set
indicator_bg = {0.18, 0.18, 0.18, 1.0}   -- inherits task_normal if not set
```

### Indicators

The battery and backlight indicators are auto-detected from sysfs. You can
override the paths or set them to `""` to disable.

```lua
battery_path = "/sys/class/power_supply/BAT0"
backlight_path = "/sys/class/backlight/amdgpu_bl1"
```

Scroll and click commands for the taskbar indicators:

```lua
brightness_scroll_up = "brightnessctl set 5%+"
brightness_scroll_down = "brightnessctl set 5%-"
volume_scroll_up = "pactl set-sink-volume @DEFAULT_SINK@ +5%"
volume_scroll_down = "pactl set-sink-volume @DEFAULT_SINK@ -5%"
volume_middle_button = "pactl set-sink-mute @DEFAULT_SINK@ toggle"
```

## Themes

Themes are directories containing a `theme.lua` and optional image assets.
The `theme.lua` can set any of the decoration and taskbar variables - they
override values from the main config.

Theme search paths (first match wins):

1. `~/.config/steppewm/themes/<name>/`
2. `/usr/share/steppewm/themes/<name>/`
3. `/usr/local/share/steppewm/themes/<name>/`

```lua
theme = "icegil-korstro-Large"
```

Themes can include PNG images (or SVG if built with librsvg) to replace the
rendered decorations. Supported filenames:

- Titlebar: `titlebar.png`, `titlebar_inactive.png`
- Buttons: `close.png`, `close_active.png`, `close_hover.png`,
  `maximize.png`, `maximize_active.png`, `maximize_hover.png`,
  `minimize.png`, `minimize_active.png`, `minimize_hover.png`
- Borders: `border_left.png`, `border_right.png`, `border_bottom.png`
- Taskbar: `taskbar_bg.png`, `taskbutton.png`, `taskbutton_active.png`,
  `taskbutton_minimized.png`, `workspace.png`, `workspace_active.png`,
  `tray_bg.png`, `indicator_bg.png`

### Converting IceWM themes

The bundled themes were converted from IceWM. `tools/icewm2steppewm.py` does
the conversion. It needs Pillow.

To convert every IceWM theme installed on the system:

```
tools/icewm2steppewm.py --all
```

This reads `/usr/share/icewm/themes/` and writes each one to
`~/.config/steppewm/themes/<name>/`. A single theme, optionally to a specific
output directory:

```
tools/icewm2steppewm.py ~/.icewm/themes/YourTheme [output-dir]
```

It parses `default.theme` for the colors, copies the pixmaps across, and
writes a `theme.lua` alongside them.

## Limits

There are some hardcoded limits: 128 key bindings, 16 switch bindings,
64 exec commands, 16 output configs, 32 pinned apps, 512-character max for
commands and action args, and 9 workspaces.
