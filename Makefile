# -*- Mode: makefile-gmake -*-

.PHONY: clean all debug release pkgconfig install install-dev

#
# Required packages
#

LDPKGS = libncicore libnciplugin libgbinder libglibutil gobject-2.0 glib-2.0
PKGS = $(LDPKGS) nfcd-plugin

#
# Default target
#

all: debug release

#
# Library name
#

NAME = nfcbinder
LIB_NAME = $(NAME)
LIB_SONAME = $(LIB_NAME).so
LIB = $(LIB_SONAME)

#
# Library version
#

VERSION_MAJOR = 1
VERSION_MINOR = 1
VERSION_RELEASE = 0

# Version for pkg-config
PCVERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_RELEASE)

#
# Sources
#

SRC = \
  binder_nfc_adapter.c \
  binder_nfc_plugin.c

#
# Directories
#

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall
BASE_FLAGS = -fPIC -fvisibility=hidden
DEFINES = -DNFC_PLUGIN_EXTERNAL
INCLUDES = -I$(INCLUDE_DIR)
FULL_CFLAGS = $(BASE_FLAGS) $(CFLAGS) $(DEFINES) $(INCLUDES) $(WARNINGS) \
  -MMD -MP $(shell pkg-config --cflags $(PKGS))
FULL_LDFLAGS = $(BASE_FLAGS) $(LDFLAGS) -shared -Wl,-soname -Wl,$(LIB_SONAME)
DEBUG_FLAGS = -g
RELEASE_FLAGS =

ifndef KEEP_SYMBOLS
KEEP_SYMBOLS = 0
endif

ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
endif

DEBUG_LDFLAGS = $(FULL_LDFLAGS) $(DEBUG_FLAGS)
RELEASE_LDFLAGS = $(FULL_LDFLAGS) $(RELEASE_FLAGS)
DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2

LIBS = $(shell pkg-config --libs $(LDPKGS))
DEBUG_LIBS = $(LIBS)
RELEASE_LIBS = $(LIBS)

#
# Files
#

PKGCONFIG = $(BUILD_DIR)/$(LIB_NAME).pc
DEBUG_OBJS = $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

#
# Dependencies
#

DEPS = \
  $(DEBUG_OBJS:%.o=%.d) \
  $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

DEBUG_DEPS =
RELEASE_DEPS =

$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#

DEBUG_LIB = $(DEBUG_BUILD_DIR)/$(LIB)
RELEASE_LIB = $(RELEASE_BUILD_DIR)/$(LIB)

debug: $(DEBUG_LIB)

release: $(RELEASE_LIB)

pkgconfig: $(PKGCONFIG)

clean:
	rm -f *~ rpm/*~ $(SRC_DIR)/*~
	rm -fr $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_LIB): $(DEBUG_OBJS) $(DEBUG_DEPS)
	$(LD) $(DEBUG_OBJS) $(DEBUG_LDFLAGS) $(DEBUG_LIBS) -o $@

$(RELEASE_LIB): $(RELEASE_OBJS) $(RELEASE_DEPS)
	$(LD) $(RELEASE_OBJS) $(RELEASE_LDFLAGS) $(RELEASE_LIBS) -o $@

$(PKGCONFIG): $(LIB_NAME).pc.in
	sed -e 's/\[version\]/'$(PCVERSION)/g $< > $@

#
# Install
#

INSTALL = install
INSTALL_DIRS = $(INSTALL) -d
INSTALL_FILES = $(INSTALL) -m 644

INSTALL_LIB_DIR = $(DESTDIR)/usr/lib/
INSTALL_PLUGIN_DIR = $(DESTDIR)/usr/lib/nfcd/plugins
INSTALL_INCLUDE_DIR = $(DESTDIR)/usr/include/$(NAME)
INSTALL_PKGCONFIG_DIR = $(DESTDIR)/usr/lib/pkgconfig

install: $(INSTALL_PLUGIN_DIR)
	$(INSTALL_FILES) $(RELEASE_LIB) $<

install-dev: $(INSTALL_LIB_DIR) $(INSTALL_INCLUDE_DIR) $(INSTALL_PKGCONFIG_DIR)
	$(INSTALL_FILES) $(RELEASE_LIB) $(INSTALL_LIB_DIR)
	$(INSTALL_FILES) $(INCLUDE_DIR)/*.h $(INSTALL_INCLUDE_DIR)
	$(INSTALL_FILES) $(PKGCONFIG) $(INSTALL_PKGCONFIG_DIR)

$(INSTALL_LIB_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_PLUGIN_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_INCLUDE_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_PKGCONFIG_DIR):
	$(INSTALL_DIRS) $@
