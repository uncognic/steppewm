https://github.com/user-attachments/assets/b725f8ec-2785-4c7b-aae8-f222e49de065

# steppewm
Stacking small wlroots window manager

If you want windows that float and overlap, with titlebars and a taskbar like IceWM, there is not much to pick from.

That's steppewm. Stacking, small and themeable
## Features

- Stacking window management, snapping, workspaces
- Taskbar and Alt-Tab switcher
- 25 classic IceWM pixmap themes included
- OSD for volume, brightness and keyboard layout
- Layer shell, session lock, multi-monitor
- System tray
- XWayland via xwayland-satellite (optional)
- Lua configuration

## Install

On Arch, from the AUR:

```
git clone https://aur.archlinux.org/steppewm-git.git
cd steppewm-git
makepkg -si
```

Elsewhere, build it. wlroots 0.20, 0.19 and 0.18 all work, so whichever your
distribution ships should be fine.

Required:

- Meson
- Ninja
- Make
- wlroots 0.20, 0.19 or 0.18
- wayland
- xkbcommon
- libinput
- lua 5.4
- pixman
- cairo

Optional:

- libpulse: volume things (taskbar and OSD)
- sdbus-c++: system tray (DBus)
- librsvg: svg icon rendering

```
% make configure
% make build
# make install
```

## Configuration

steppewm is configured through config.lua, which is looked for at
`$XDG_CONFIG_HOME/steppewm/config.lua` (`~/.config/steppewm/config.lua`).
A custom path can be passed with `steppewm -c /path/to/config.lua`

See the example config.lua for config options. The full reference,
including theming and the IceWM theme converter, is in
[doc/steppewm.md](doc/steppewm.md).

## Useful Stuff
- swaybg
- swayidle
- wlr-randr
- wlrctl
- wlopm
- xwayland-satellite
