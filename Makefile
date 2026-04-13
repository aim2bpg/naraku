# V=0 quiet, V=1 verbose. Other values don't work.
V = 0
V0 = $(V:0=)
Q1 = $(V:1=)
Q = $(Q1:0=@)
ECHO1 = $(V:1=@ :)
ECHO = $(ECHO1:0=@ echo)

# Utility commands.
MAKEDIRS ?= mkdir -p
RMALL ?= rm -f -r

# Source and artifact files.
HEADERS := $(wildcard include/*.h include/encoding/*.h)
SOURCES := $(wildcard src/*.c src/encoding/*.c)
FORMAT_FILES := $(HEADERS) $(SOURCES) $(wildcard mrbgems/*/src/*.h mrbgems/*/src/*.c)
STATIC_OBJECTS := $(subst src/,build/static/,$(SOURCES:.c=.o))

# Compilers.
CC ?= cc
AR ?= ar

# Compiler flags.
CPPFLAGS := -Iinclude $(CPPFLAGS)
CFLAGS := -g -O2 -std=c99 -Wall -Werror -Wextra -Wpedantic -Wundef -Wconversion -Wno-missing-braces -fPIC -fvisibility=hidden -Wimplicit-fallthrough $(CFLAGS)
ARFLAGS ?= -r$(V0:1=v)

build/libnaraku.a: $(STATIC_OBJECTS)
	$(ECHO) "building $@ with $(AR)"
	$(Q) $(AR) $(ARFLAGS) $@ $(STATIC_OBJECTS)

build/static/%.o: src/%.c Makefile $(HEADERS)
	$(ECHO) "compiling $@"
	$(Q) $(MAKEDIRS) $(@D)
	$(Q) $(CC) $(DEBUG_FLAGS) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

build/static/cprop.o: src/.gen/name2cprop.gen.h
build/static/encoding_ascii.o: src/.gen/cprop_range_ascii.gen.h src/.gen/case_map_ascii.gen.h
build/static/encoding_unicode.o: src/.gen/cprop_range_unicode.gen.h src/.gen/case_map_unicode.gen.h
build/static/encoding/iso_8859_1.o: src/encoding/.gen/cprop_range_iso_8859_1.gen.h src/encoding/.gen/case_map_iso_8859_1.gen.h
build/static/encoding/shift_jis.o: src/encoding/.gen/cprop_range_shift_jis.gen.h src/encoding/.gen/case_map_shift_jis.gen.h

.PHONY: format
format:
	$(ECHO) "formatting C files"
	$(Q) clang-format -i $(FORMAT_FILES)

.PHONY: clean
clean:
	$(ECHO) "cleaning build artifacts"
	$(Q) $(RMALL) build
