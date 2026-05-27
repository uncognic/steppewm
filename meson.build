project('parwm', 'c',
  version : '0.1.0',
  default_options : [
    'c_std=gnu23',
    'warning_level=2',
  ],
)

add_project_arguments('-DWLR_USE_UNSTABLE', language : 'c')
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language : 'c')

wlroots    = dependency('wlroots-0.19')
wayland    = dependency('wayland-server')
xkbcommon  = dependency('xkbcommon')
lua = dependency('lua5.4', 'lua-5.4', 'lua', version : '>=5.4')
pixman     = dependency('pixman-1')

deps = [wlroots, wayland, xkbcommon, lua, pixman]

wl_protocols = dependency('wayland-protocols')
wl_protocol_dir = wl_protocols.get_variable('pkgdatadir')
wl_scanner = find_program('wayland-scanner')

protocols = [
  wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
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
  'src/main.c',
  'src/output.c',
  'src/view.c',
  'src/input.c',
  'src/deco.c',
)

executable('parwm',
  sources,
  protocol_headers,
  dependencies : deps,
  install : true,
)