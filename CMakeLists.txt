cmake_minimum_required(VERSION 3.20)
project(fbterm)

include(CheckIncludeFile)

find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM IMPORTED_TARGET libdrm)
pkg_check_modules(LIBINPUT IMPORTED_TARGET libinput>=1.19)
pkg_check_modules(LIBUDEV IMPORTED_TARGET libudev)
pkg_check_modules(LIBDBUS IMPORTED_TARGET dbus-1)

option(USE_SDL2 "Enable optional SDL2 backend (dev-only, off by default)" OFF)
if (USE_SDL2)
	pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
endif()

set(CMAKE_CXX_STANDARD 17)

check_include_file("sys/epoll.h" HAS_EPOLL)
check_include_file("sys/signalfd.h" HAS_SIGNALFD)

add_executable(fbterm
	src/lib/charsetmap.cpp
	src/lib/io.cpp
	src/lib/shell.cpp
	src/lib/vterm.cpp
	src/lib/vterm_action.cpp
	src/lib/vterm_states.cpp
	src/lib/wcwidth.cpp

	im/imapi.c
	im/keycode.c

	src/config.cpp
	src/fbdev.cpp
	src/fbio.cpp
	src/fbshell.cpp
	src/fbshellman.cpp
	src/fbterm.cpp
	src/font.cpp
	src/idle_timer.cpp
	src/improxy.cpp
	src/input.cpp
	src/input_generic.cpp
	src/mouse.cpp
	src/screen.cpp
	src/screen_render.cpp
	src/vesadev.cpp
)

add_executable(angbd EXCLUDE_FROM_ALL
	util/angbd.c
)

target_link_libraries(fbterm PRIVATE Freetype::Freetype Fontconfig::Fontconfig)
target_include_directories(fbterm PRIVATE src src/lib)
target_compile_definitions(fbterm PRIVATE VERSION="2.0")

if (HAS_EPOLL)
	target_compile_definitions(fbterm PRIVATE HAVE_EPOLL)
	message(STATUS "Using EPOLL")
endif()

if (HAS_SIGNALFD)
	target_compile_definitions(fbterm PRIVATE HAVE_SIGNALFD)
	message(STATUS "Using SIGNALFD")
endif()

if (LIBINPUT_FOUND AND LIBUDEV_FOUND)
	target_link_libraries(fbterm PRIVATE PkgConfig::LIBINPUT PkgConfig::LIBUDEV)
	target_compile_definitions(fbterm PRIVATE ENABLE_LIBINPUT)
	message(STATUS "Using libinput")
endif()

if (LIBDRM_FOUND AND LIBDBUS_FOUND)
	target_sources(fbterm PRIVATE src/drmdev.cpp src/dbus.cpp)
	target_link_libraries(fbterm PRIVATE PkgConfig::LIBDRM PkgConfig::LIBDBUS)
	target_compile_definitions(fbterm PRIVATE ENABLE_DRM)
	message(STATUS "Using DRM")
endif()

if (SDL2_FOUND)
	target_sources(fbterm PRIVATE src/sdl2.cpp src/input_sdl2.cpp)
	target_link_libraries(fbterm PRIVATE PkgConfig::SDL2)
	target_compile_definitions(fbterm PRIVATE ENABLE_SDL2)
	message(STATUS "Using SDL2")
endif()

install(TARGETS fbterm)
