# -*- Mode: makefile-gmake -*-

.PHONY: clean all debug release install

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

NAME = binder
LIB_NAME = $(NAME)
LIB_SONAME = $(LIB_NAME).so
LIB = $(LIB_SONAME)

#
# Sources
#

SRC = \
  binder_nfc_adapter.c \
  binder_nfc_api.c \
  binder_nfc_api_hidl.c \
  binder_nfc_plugin.c \
  binder_nfc_watcher.c

#
# Directories
#

SRC_DIR = src
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
FULL_CFLAGS = $(BASE_FLAGS) $(CFLAGS) $(DEFINES) $(WARNINGS) -MMD -MP \
  $(shell pkg-config --cflags $(PKGS))
FULL_LDFLAGS = $(BASE_FLAGS) $(LDFLAGS) -shared
DEBUG_FLAGS = -g
RELEASE_FLAGS =

DISABLE_HEXDUMP ?= 0
ifneq ($(DISABLE_HEXDUMP),0)
DEFINES += -DDISABLE_HEXDUMP
endif

KEEP_SYMBOLS ?= 0
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

clean:
	rm -f *~ rpm/*~ $(SRC_DIR)/*~
	rm -fr $(BUILD_DIR)

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

#
# Install
#

PLUGIN_DIR ?= usr/lib/nfcd/plugins
ABS_PLUGIN_DIR := $(shell echo /$(PLUGIN_DIR) | sed -r 's|/+|/|g')

INSTALL = install
INSTALL_DIRS = $(INSTALL) -d
INSTALL_PLUGIN_DIR = $(DESTDIR)$(ABS_PLUGIN_DIR)

install: $(INSTALL_PLUGIN_DIR)
	$(INSTALL) -m 755 $(RELEASE_LIB) $(INSTALL_PLUGIN_DIR)

$(INSTALL_PLUGIN_DIR):
	$(INSTALL_DIRS) $@
