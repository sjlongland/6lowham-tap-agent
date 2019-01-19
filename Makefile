# SPDX-License-Identifier: GPL-2.0+
# vim: set tw=78 ts=8 sts=8 noet fileencoding=utf-8:

# Determine the user's OS
OS ?= $(shell uname -s | tr A-Z a-z)

# Collect the user's compiler/linker settings
CFLAGS := $(CFLAGS)
CPPFLAGS := $(CPPFLAGS)
LDFLAGS := $(LDFLAGS)

ifeq ($(OS),linux)
# Add in libnl CFLAGS/LIBS
CPPFLAGS += $(shell pkg-config --cflags libnl-3.0)
LDFLAGS += $(shell pkg-config --libs libnl-3.0)

# Add in libnl-route CFLAGS/LIBS
CPPFLAGS += $(shell pkg-config --cflags libnl-route-3.0)
LDFLAGS += $(shell pkg-config --libs libnl-route-3.0)
endif

# --- Shouldn't need to touch things below here ---

# All build targets
TARGETS := 6lhagent

# All targets
all: $(TARGETS)

# All source files, except OS-specific tap interfaces.
SOURCES := $(filter-out %tap.c,$(wildcard *.c))

ifeq ($(OS),linux)
SOURCES += linuxtap.c
endif

# All object files and dependencies
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))
DEPENDENCIES := $(patsubst %.c,%.d,$(SOURCES))

# Clean-up target
clean:
	-rm -fr $(OBJECTS) $(DEPENDENCIES) $(TARGETS)

6lhagent: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-MM -o $(patsubst %.c,%.d,$<) $<
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

-include $(DEPENDENCIES)

.PHONY: clean all
