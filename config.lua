-- steppewm config

bind("Super", "Return", "spawn", "foot")
bind("Super", "d", "spawn", "wofi --show drun")
bind("Super", "Escape", "quit")
bind("Alt", "Tab", "focus_next")
bind("Super", "Down", "minimize")
bind("Super", "Up", "maximize")
bind("Super", "q", "close")

taskbar_all_outputs = true

-- colors {r, g, b, a}
-- title_active = {0.24, 0.24, 0.24, 1.0}
-- title_inactive = {0.14, 0.14, 0.14, 1.0}
-- border_color = {0.20, 0.20, 0.20, 1.0}
-- close_active = {0.85, 0.08, 0.08, 1.0}
-- close_inactive = {0.45, 0.06, 0.06, 1.0}
-- button_color = {0.38, 0.38, 0.38, 1.0}
-- button_inactive= {0.32, 0.32, 0.32, 1.0}

-- title_height = 20
-- border_width = 3
-- corner_size = 8
-- close_button_width = 40
-- minimize_button_width = 20

-- taskbar
-- taskbar_height = 24
-- taskbar_bg = {0.08, 0.08, 0.08, 1.0}
-- task_normal = {0.18, 0.18, 0.18, 1.0}
-- task_active = {0.30, 0.30, 0.30, 1.0}
-- task_minimized = {0.12, 0.12, 0.12, 1.0}
-- task_text = {0.88, 0.88, 0.88, 1.0}