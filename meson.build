project('steppewm', ['c', 'cpp'],
  version : '0.1.0',
  default_options : [
    'c_std=gnu23',
    'cpp_std=c++20',
    'warning_level=2',
  ],
)

add_project_arguments('-DWLR_USE_UNSTABLE', language : ['c', 'cpp'])
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language : ['c', 'cpp'])

wlroots = dependency('wlroots-0.19')
wayland = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput')
lua = dependency('lua5.4', 'lua-5.4', 'lua', version : '>=5.4')
pixman = dependency('pixman-1')
cairo = dependency('cairo')

deps = [wlroots, wayland, xkbcommon, libinput, lua, pixman, cairo]

wl_protocols = dependency('wayland-protocols')
wl_protocol_dir = wl_protocols.get_variable('pkgdatadir')
wl_scanner = find_program('wayland-scanner')

protocols = [
  wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
  wl_protocol_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
  'protocol/wlr-layer-shell-unstable-v1.xml',
]

protocol_headers = []
foreach xml : protocols
  protocol_headers += custom_target(
    input : xml,
    output : '@BASENAME@-protocol.h',
    command : [wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

sources = files(
  'src/main.cxx',
  'src/config.cxx',
  'src/output.cxx',
  'src/view.cxx',
  'src/input.cxx',
  'src/deco.cxx',
  'src/taskbar.cxx',
  'src/switcher.cxx',
  'src/paint.cxx',
  'src/layer.cxx',
  'src/listener.cxx',
)

executable('steppewm',
  sources,
  protocol_headers,
  dependencies : deps,
  install : true,
)