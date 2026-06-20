cmake_minimum_required(VERSION 3.25)
project(steppewm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(
        WLR_USE_UNSTABLE
        _POSIX_C_SOURCE=200809L
)
add_compile_options(-Wall -Wextra)

find_package(PkgConfig REQUIRED)

pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.19)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)

set(LUA_NAMES lua5.4 lua-5.4 lua)
foreach (name ${LUA_NAMES})
    pkg_check_modules(LUA IMPORTED_TARGET ${name})
    if (LUA_FOUND AND LUA_VERSION VERSION_GREATER_EQUAL "5.4")
        break()
    endif ()
endforeach ()
if (NOT LUA_FOUND)
    message(FATAL_ERROR "lua >= 5.4 not found")
endif ()

set(DEPS
        PkgConfig::WLROOTS
        PkgConfig::WAYLAND
        PkgConfig::XKBCOMMON
        PkgConfig::LIBINPUT
        PkgConfig::LUA
        PkgConfig::PIXMAN
        PkgConfig::CAIRO
)

pkg_check_modules(PULSE IMPORTED_TARGET libpulse)
if (PULSE_FOUND)
    list(APPEND DEPS PkgConfig::PULSE)
    add_compile_definitions(HAVE_LIBPULSE)
endif ()

pkg_check_modules(SDBUS IMPORTED_TARGET sdbus-c++)
if (SDBUS_FOUND)
    list(APPEND DEPS PkgConfig::SDBUS)
    add_compile_definitions(HAVE_SDBUS)
endif ()

pkg_check_modules(RSVG IMPORTED_TARGET librsvg-2.0>=2.46)
if (RSVG_FOUND)
    list(APPEND DEPS PkgConfig::RSVG)
    add_compile_definitions(HAVE_LIBRSVG)
endif ()

pkg_get_variable(WL_PROTOCOL_DIR wayland-protocols pkgdatadir)
find_program(WL_SCANNER wayland-scanner REQUIRED)

set(PROTOCOLS
        ${WL_PROTOCOL_DIR}/stable/xdg-shell/xdg-shell.xml
        ${WL_PROTOCOL_DIR}/staging/cursor-shape/cursor-shape-v1.xml
        ${WL_PROTOCOL_DIR}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
        ${CMAKE_SOURCE_DIR}/protocol/wlr-layer-shell-unstable-v1.xml
        ${CMAKE_SOURCE_DIR}/protocol/wlr-output-power-management-unstable-v1.xml
        ${WL_PROTOCOL_DIR}/staging/tearing-control/tearing-control-v1.xml
)

set(PROTOCOL_HEADERS)
foreach (xml ${PROTOCOLS})
    get_filename_component(base ${xml} NAME_WE)
    set(header ${CMAKE_BINARY_DIR}/protocol/${base}-protocol.h)
    add_custom_command(
            OUTPUT ${header}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/protocol
            COMMAND ${WL_SCANNER} server-header ${xml} ${header}
            DEPENDS ${xml}
            VERBATIM
    )
    list(APPEND PROTOCOL_HEADERS ${header})
endforeach ()

add_custom_target(protocols DEPENDS ${PROTOCOL_HEADERS})

add_executable(steppewm
        src/main.cc
        src/config.cc
        src/output.cc
        src/view.cc
        src/input.cc
        src/deco.cc
        src/taskbar.cc
        src/switcher.cc
        src/paint.cc
        src/theme.cc
        src/layer.cc
        src/listener.cc
        src/lock.cc
        src/osd.cc
        src/volume.cc
        src/tray.cc
)

add_dependencies(steppewm protocols)
target_include_directories(steppewm PRIVATE ${CMAKE_BINARY_DIR}/protocol)
target_link_libraries(steppewm PRIVATE ${DEPS})

install(TARGETS steppewm RUNTIME DESTINATION bin)
install(FILES steppewm.desktop DESTINATION share/wayland-sessions)
install(DIRECTORY themes/ DESTINATION share/steppewm/themes)
