SUBDIRS = lib

bin_PROGRAMS = fbterm

fbterm_SOURCES = fbconfig.cpp fbconfig.h fbio.cpp fbio.h fbshell.cpp fbshell.h fbshellman.cpp fbshellman.h fbterm.cpp \
	fbterm.h font.cpp font.h input.cpp input.h input_key.h mouse.cpp mouse.h screen.cpp screen.h improxy.cpp improxy.h \
	screen_render.cpp fbdev.cpp fbdev.h vesadev.cpp vesadev.h vbe.h
EXTRA_fbterm_SOURCES = signalfd.h

fbterm_CXXFLAGS = -fno-exceptions -fno-rtti -Ilib @FT2_INC@ @FC_INC@
fbterm_LDADD = lib/libshell.a @FT2_LIB@ @FC_LIB@ @X86_LIB@ -lutil

if VESA
install-exec-hook:
	[[ "`id -u`" != "0" ]] && { \
		echo "run 'make install' as root!"; \
		exit 1; \
	} || { \
		chmod u+s $(DESTDIR)$(bindir)/fbterm$(EXEEXT); \
	}
else
install-exec-hook:
	KERV=`uname -r | sed 's/\([0-9]*\)\.\([0-9]*\)\.\([0-9]*\).*/\1 \* 10000 + \2 \* 100 + \3/'`; \
	[[ $$KERV -lt 20615 ]] && exit 0; \
	[[ "`id -u`" != "0" ]] && { \
		echo "run 'make install' as root!"; \
		exit 1; \
	} || { \
		[[ $$KERV -ge 20627 ]] && \
		setcap 'cap_sys_tty_config+ep' $(DESTDIR)$(bindir)/fbterm$(EXEEXT) 2>/dev/null || \
		chmod u+s $(DESTDIR)$(bindir)/fbterm$(EXEEXT); \
	}
endif
