#!/usr/bin/env python3
# Convert an IceWM theme to a SteppeWM theme

import os
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("pip install Pillow", file=sys.stderr)
    sys.exit(1)


# parse default.theme file into a dict of key=value pairs
def parse_theme_file(theme_dir):
    cfg = {}
    theme_file = theme_dir / "default.theme"
    if not theme_file.exists():
        return cfg
    with open(theme_file) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r'(\w+)\s*=\s*"?([^"]*)"?\s*$', line)
            if m:
                cfg[m.group(1)] = m.group(2).strip()
    return cfg


# parse a color string (rgb:RR/GG/BB or #RRGGBB) into a (r, g, b) float tuple
def parse_rgb(s):
    if not s:
        return None
    s = s.strip().strip('"')
    m = re.match(r"rgb:([0-9a-fA-F]{2})/([0-9a-fA-F]{2})/([0-9a-fA-F]{2})", s)
    if m:
        return tuple(int(x, 16) / 255.0 for x in m.groups())
    m = re.match(r"#([0-9a-fA-F]{6})", s)
    if m:
        h = m.group(1)
        return (int(h[0:2], 16) / 255.0, int(h[2:4], 16) / 255.0, int(h[4:6], 16) / 255.0)
    return None


# search for a pixmap file by name, trying multiple extensions and subdirectories
def find_pixmap(theme_dir, *names):
    for name in names:
        for ext in (".png", ".xpm", ".jpg"):
            for subdir in ("", "taskbar"):
                p = theme_dir / subdir / (name + ext)
                if p.exists():
                    return p
    return None


# open an image file with Pillow
def try_open(path):
    try:
        return Image.open(path)
    except Exception:
        pass
    if path.suffix == ".xpm":
        try:
            with open(path) as f:
                fixed = f.read().replace("\tg ", "\tc ").replace("\tg\t", "\tc\t")
            import tempfile
            with tempfile.NamedTemporaryFile(suffix=".xpm", mode="w", delete=False) as tmp:
                tmp.write(fixed)
                tmp.flush()
                img = Image.open(tmp.name)
                os.unlink(tmp.name)
                return img
        except Exception as e:
            print(f"  skipping {path.name}: {e}", file=sys.stderr)
            return None
    print(f"  skipping {path.name}: unsupported format", file=sys.stderr)
    return None


# convert an image to RGBA, stripping any legacy transparency info
def to_rgba(img):
    img.info.pop("transparency", None)
    return img.convert("RGBA") if img.mode != "RGBA" else img


# icewm buttons have 2 states in a single image: top half = normal, bottom half = pressed
def split_button(img):
    img = to_rgba(img)
    w, h = img.size
    state_h = h // 2
    return img.crop((0, 0, w, state_h)), img.crop((0, state_h, w, h))


# save an image as RGBA PNG
def save_png(img, path):
    to_rgba(img).save(path, "PNG")


# convert a full IceWM theme directory into SteppeWM format
def convert(icewm_dir, output_dir):
    icewm_dir = Path(icewm_dir)
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    cfg = parse_theme_file(icewm_dir)

    for btn in ("close", "maximize", "minimize"):
        # try A suffix first (active/focused), fall back to no suffix
        active_src = find_pixmap(icewm_dir, btn + "A", btn)
        inactive_src = find_pixmap(icewm_dir, btn + "I", btn)
        hover_src = find_pixmap(icewm_dir, btn + "O")

        if active_src:
            img = try_open(active_src)
            if img:
                normal, pressed = split_button(img)
                save_png(normal, output_dir / f"{btn}_active.png")
                if not hover_src:
                    save_png(pressed, output_dir / f"{btn}_hover.png")

        if inactive_src:
            img = try_open(inactive_src)
            if img:
                normal, _ = split_button(img)
                save_png(normal, output_dir / f"{btn}.png")

        if hover_src:
            img = try_open(hover_src)
            if img:
                normal, _ = split_button(img)
                save_png(normal, output_dir / f"{btn}_hover.png")

        if not active_src and not inactive_src:
            src = find_pixmap(icewm_dir, btn)
            if src:
                img = try_open(src)
                if img:
                    normal, pressed = split_button(img)
                    save_png(normal, output_dir / f"{btn}.png")
                    save_png(normal, output_dir / f"{btn}_active.png")
                    save_png(pressed, output_dir / f"{btn}_hover.png")

    # titleAB = active background, titleIB = inactive background
    tb_active = find_pixmap(icewm_dir, "titleAB")
    tb_inactive = find_pixmap(icewm_dir, "titleIB")

    if tb_active:
        img = try_open(tb_active)
        if img:
            save_png(img, output_dir / "titlebar.png")

    if tb_inactive:
        img = try_open(tb_inactive)
        if img:
            save_png(img, output_dir / "titlebar_inactive.png")

    # borders: frameAL/AR/AB = active left/right/bottom
    for edge, suffix in [("left", "L"), ("right", "R"), ("bottom", "B")]:
        src = find_pixmap(icewm_dir, "frameA" + suffix, "dframeA" + suffix)
        if src:
            img = try_open(src)
            if img:
                save_png(img, output_dir / f"border_{edge}.png")

    # taskbar pixmaps
    for icewm_name, steppe_name in [
        ("taskbarbg", "taskbar_bg"),
        ("taskbuttonbg", "taskbutton"),
        ("taskbuttonactive", "taskbutton_active"),
        ("taskbuttonminimized", "taskbutton_minimized"),
        ("workspacebuttonbg", "workspace"),
        ("workspacebuttonactive", "workspace_active"),
    ]:
        src = find_pixmap(icewm_dir, icewm_name)
        if src:
            img = try_open(src)
            if img:
                save_png(img, output_dir / f"{steppe_name}.png")

    # generate theme.lua from default.theme settings
    lua_lines = [f"-- converted from IceWM theme: {icewm_dir.name}"]

    title_h = cfg.get("TitleBarHeight")
    if title_h:
        lua_lines.append(f"title_height = {title_h}")

    border_x = cfg.get("BorderSizeX")
    if border_x:
        lua_lines.append(f"border_width = {border_x}")

    centered = cfg.get("TitleBarJustify", "0")
    if centered == "50":
        lua_lines.append("center_title_text = true")

    buttons_left_str = cfg.get("TitleButtonsLeft", "")
    if "x" in buttons_left_str:
        lua_lines.append("buttons_left = true")

    # colors
    color_map = {
        "ColorNormalTitleBar": "title_active",
        "ColorActiveTitleBar": None,
        "ColorNormalTitleBarText": None,
        "ColorActiveTitleBarText": None,
        "ColorDefaultTaskBar": "taskbar_bg",
        "ColorNormalTaskBarApp": "task_normal",
        "ColorActiveTaskBarApp": "task_active",
        "ColorMinimizedTaskBarApp": "task_minimized",
        "ColorNormalTaskBarAppText": "task_text",
    }

    for icewm_key, steppe_key in color_map.items():
        if steppe_key and icewm_key in cfg:
            rgb = parse_rgb(cfg[icewm_key])
            if rgb:
                lua_lines.append(f"{steppe_key} = {{{rgb[0]:.2f}, {rgb[1]:.2f}, {rgb[2]:.2f}, 1.0}}")

    # title text color from active titlebar text
    for key in ("ColorActiveTitleBarText", "ColorNormalTitleBarText"):
        if key in cfg:
            rgb = parse_rgb(cfg[key])
            if rgb:
                lua_lines.append(f"title_text = {{{rgb[0]:.2f}, {rgb[1]:.2f}, {rgb[2]:.2f}, 1.0}}")
                break

    with open(output_dir / "theme.lua", "w") as f:
        f.write("\n".join(lua_lines) + "\n")

    print(f"converted {icewm_dir.name} -> {output_dir}")
    print(f"  pixmaps: {len(list(output_dir.glob('*.png')))}")
    print(f"  theme.lua generated")


def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <icewm-theme-dir> [output-dir]")
        print(f"       {sys.argv[0]} --all  (convert all from /usr/share/icewm/themes/)")
        sys.exit(1)

    if sys.argv[1] == "--all":
        icewm_base = Path("/usr/share/icewm/themes")
        home = os.environ.get("HOME", "")
        out_base = Path(f"{home}/.config/steppewm/themes")
        for d in sorted(icewm_base.iterdir()):
            if d.is_dir() and (d / "default.theme").exists():
                convert(d, out_base / d.name)
    else:
        icewm_dir = Path(sys.argv[1])
        if len(sys.argv) > 2:
            output_dir = Path(sys.argv[2])
        else:
            home = os.environ.get("HOME", "")
            output_dir = Path(f"{home}/.config/steppewm/themes/{icewm_dir.name}")
        convert(icewm_dir, output_dir)


if __name__ == "__main__":
    main()
