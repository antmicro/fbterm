check_PROGRAMS = imexample

imexample_SOURCES = imapi.h imapi.c keycode.h keycode.c example.cpp font.h font.cpp screen.h screen.cpp
EXTRA_imexample_SOURCES = screen_render.cpp

imexample_CXXFLAGS = -fno-exceptions -fno-rtti @FT2_INC@ @FC_INC@
imexample_LDADD = @FT2_LIB@ @FC_LIB@
