project(
	'steppewm',
	'cpp',
	version: '0.3.3',
	license: 'GPL-3.0-or-later',
	meson_version: '>= 1.0.0',
	default_options: [
		'cpp_std=c++20',
		'warning_level=2',
		'buildtype=release',
	],
)

add_project_arguments(
	[
		'-DWLR_USE_UNSTABLE',
		'-D_POSIX_C_SOURCE=200809L',
		'-DSTEPPEWM_VERSION="@0@"'.format(meson.project_version()),
	],
	language: 'cpp',
)

wlroots = disabler()
wlroots_version = ''
foreach version : ['0.20', '0.19', '0.18']
	if wlroots_version == ''
		candidate = dependency('wlroots-' + version, required: false)
		if candidate.found()
			wlroots = candidate
			wlroots_version = version
		endif
	endif
endforeach
if wlroots_version == ''
	error('none of wlroots-0.20, wlroots-0.19 or wlroots-0.18 were found')
endif

lua = disabler()
foreach name : ['lua5.4', 'lua-5.4', 'lua54', 'lua']
	if not lua.found()
		lua = dependency(name, version: '>= 5.4', required: false)
	endif
endforeach
if not lua.found()
	error('lua >= 5.4 is required')
endif

deps = [
	wlroots,
	lua,
	dependency('wayland-server'),
	dependency('xkbcommon'),
	dependency('libinput'),
	dependency('pixman-1'),
	dependency('cairo'),
]

pulse = dependency('libpulse', required: get_option('pulse'))
if pulse.found()
	deps += pulse
	add_project_arguments('-DHAVE_LIBPULSE', language: 'cpp')
endif

sdbus = dependency('sdbus-c++', required: get_option('sdbus'))
if sdbus.found()
	deps += sdbus
	add_project_arguments('-DHAVE_SDBUS', language: 'cpp')
endif

rsvg = dependency('librsvg-2.0', version: '>= 2.46', required: get_option('rsvg'))
if rsvg.found()
	deps += rsvg
	add_project_arguments('-DHAVE_LIBRSVG', language: 'cpp')
endif

shim_dir = meson.current_build_dir() / 'wlr-cxx-shim'
shim = run_command(
	find_program('sh'),
	files('tools/wlr-cxx-shim.sh'),
	'wlroots-' + wlroots_version,
	shim_dir,
	check: true,
)
shim_include = include_directories('wlr-cxx-shim')

wayland_scanner = find_program('wayland-scanner')
wayland_protocols = dependency('wayland-protocols')
protocol_dir = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir')

protocols = [
	protocol_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml',
	protocol_dir / 'stable' / 'tablet' / 'tablet-v2.xml',
	protocol_dir / 'staging' / 'cursor-shape' / 'cursor-shape-v1.xml',
	protocol_dir / 'staging' / 'tearing-control' / 'tearing-control-v1.xml',
	protocol_dir / 'unstable' / 'pointer-constraints' / 'pointer-constraints-unstable-v1.xml',
	'protocol/wlr-layer-shell-unstable-v1.xml',
	'protocol/wlr-output-power-management-unstable-v1.xml',
]

server_header = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.h',
	arguments: ['server-header', '@INPUT@', '@OUTPUT@'],
)
protocol_headers = server_header.process(protocols)

sources = files(
	'src/config.cc',
	'src/deco.cc',
	'src/input.cc',
	'src/layer.cc',
	'src/listener.cc',
	'src/lock.cc',
	'src/main.cc',
	'src/osd.cc',
	'src/output.cc',
	'src/paint.cc',
	'src/switcher.cc',
	'src/tablet.cc',
	'src/taskbar.cc',
	'src/theme.cc',
	'src/tray.cc',
	'src/view.cc',
	'src/volume.cc',
)

executable(
	'steppewm',
	sources + protocol_headers,
	include_directories: shim_include,
	dependencies: deps,
	install: true,
)

install_data(
	'steppewm.desktop',
	install_dir: get_option('datadir') / 'wayland-sessions',
)
install_subdir(
	'themes',
	install_dir: get_option('datadir') / 'steppewm',
)

summary(
	{
		'wlroots': wlroots_version,
		'lua': lua.version(),
		'libpulse (volume)': pulse.found(),
		'sdbus-c++ (tray)': sdbus.found(),
		'librsvg (svg icons)': rsvg.found(),
	},
	section: 'steppewm',
	bool_yn: true,
)
