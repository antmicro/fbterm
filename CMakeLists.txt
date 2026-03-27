cmake_minimum_required(VERSION 3.20)
project(fbterm)

include(CheckIncludeFile)

find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)

set(CMAKE_CXX_STANDARD 11)

check_include_file("sys/epoll.h" HAS_EPOLL)
check_include_file("sys/signalfd.h" HAS_SIGNALFD)
check_include_file("gpm.h" HAS_GPM)

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
	src/improxy.cpp
	src/input.cpp
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

if (HAS_GPM)
	target_link_libraries(fbterm PRIVATE gpm)
	target_compile_definitions(fbterm PRIVATE ENABLE_GPM)
	message(STATUS "Using GPM")
endif()

install(TARGETS fbterm)
