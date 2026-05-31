-- steppewm config

local terminal = "foot"
local launcher = "wofi --show drun"
local file_manager = "thunar"

bind("Super", "Return", "spawn", terminal)
bind("Super", "d", "spawn", launcher)
bind("Super", "e", "spawn", file_manager)
bind("Super", "Escape", "quit")
bind("Alt", "Tab", "focus_next")
bind("Super", "Down", "minimize")
bind("Super", "Up", "maximize")
bind("Super", "q", "close")
exec("swaybg -i ~/Pictures/Vallpapers/bolatbek-gabiden-dsL_tvf1Z-E-unsplash.jpg -m fill")
-- exec("firefox")

-- colors {r, g, b, a}
-- title_active = {0.24, 0.24, 0.24, 1.0}
-- title_inactive = {0.14, 0.14, 0.14, 1.0}
-- border_color = {0.20, 0.20, 0.20, 1.0}
-- close_active = {0.85, 0.08, 0.08, 1.0}
-- close_inactive = {0.45, 0.06, 0.06, 1.0}
-- button_color = {0.38, 0.38, 0.38, 1.0}
-- button_inactive= {0.32, 0.32, 0.32, 1.0}W

-- title_height = 20
-- border_width = 3
-- corner_size = 8
-- close_button_width = 40
-- minimize_button_width = 20

-- taskbar
-- taskbar_all_outputs = true
-- taskbar_button_width = 200
-- taskbar_button_pad = 2
-- taskbar_height = 24
-- taskbar_bg = {0.08, 0.08, 0.08, 1.0}
-- task_normal = {0.18, 0.18, 0.18, 1.0}
-- task_active = {0.30, 0.30, 0.30, 1.0}
-- task_minimized = {0.12, 0.12, 0.12, 1.0}
-- task_text = {0.88, 0.88, 0.88, 1.0}
