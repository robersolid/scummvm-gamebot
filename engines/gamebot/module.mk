MODULE := engines/gamebot

MODULE_OBJS = \
	gamebot.o \
	character.o \
	console.o \
	debug-names.o \
	logic.o \
	metaengine.o \
	resource.o \
	world.o

# This module can be built as a plugin
ifeq ($(ENABLE_GAMEBOT), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
