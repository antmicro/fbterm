noinst_PROGRAMS = imexample

imexample_SOURCES = immessage.h imapi.h imapi.c keycode.h keycode.c example.cpp \
	instance.h font.h font.cpp screen.h screen.cpp
EXTRA_imexample_SOURCES = screen_render.cpp

imexample_CXXFLAGS = -fno-exceptions -fno-rtti @IMEXAMPLE_INCLUDES@
imexample_LDADD = @IMEXAMPLE_LIBS@

EXTRA_DIST = inputmethod.txt
