# Shared build rules for Atari Lynx projects in this repo (cc65, target lynx).
#
# A project Makefile is just:
#
#   NAME := mygame
#   include ../../sdk/project.mk
#
# Layout:
#   src/*.c src/*.s   -> compiled (cc65 -Oirs, target lynx) and linked
#   sdk/src/*.c *.s   -> build/lynxsdk.lib (a program links only what it uses)
#
# Outputs:  build/<NAME>.lnx   cartridge image (LNX header + cc65 boot loader)
#           build/<NAME>.map   ld65 map (segments, symbols)
#           build/<NAME>.lbl   VICE-style labels (./lynx symbols, emu_break("func"))
#           build/<NAME>.dbg   ld65 debug info (source lines)
#
# Memory map: sdk/cfg/lynx.cfg (code from $0200, 2 KB C stack below $BE38,
# screen buffers $C038/$E018).
#
# Overridable: OPT (default -Oirs), CFG (linker config), EXTRA_CFLAGS,
#   EXTRA_ASFLAGS, EXTRA_LDFLAGS, EXTRA_OBJS, EXTRA_SRC_DIRS, RELEASE=1 (no
#   harness log: lynx_log compiles away), DEBUG=1 (-O0; -g is always on: it
#   only adds symbols to the .lbl/.dbg), LYNX_NO_LOG=1
#
# Run via bin/lynx-make (or ./lynx build), which supplies the toolchain.

ifndef NAME
$(error set NAME before including project.mk)
endif

SDK_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

CC65_TARGET ?= lynx
CFG     ?= $(SDK_DIR)/cfg/lynx.cfg
SRC_DIR ?= src
BUILD   ?= build
OBJDIR  := $(BUILD)/obj

ifeq ($(DEBUG),1)
OPT ?= -O0 -g
else
OPT ?= -Oirs -g
endif

DEFS    := $(if $(filter 1,$(LYNX_NO_LOG)),-DLYNX_NO_LOG) $(if $(filter 1,$(RELEASE)),-DLYNX_RELEASE)
CFLAGS   = -t $(CC65_TARGET) $(OPT) -I$(SRC_DIR) $(addprefix -I,$(EXTRA_SRC_DIRS)) -I$(SDK_DIR)/include $(DEFS) $(EXTRA_CFLAGS)
ASFLAGS  = -t $(CC65_TARGET) -g -I$(SRC_DIR) -I$(SDK_DIR)/include $(EXTRA_ASFLAGS)
LDFLAGS  = -t $(CC65_TARGET) -C $(CFG) \
           -m $(BUILD)/$(NAME).map -Ln $(BUILD)/$(NAME).lbl -Wl --dbgfile,$(BUILD)/$(NAME).dbg $(EXTRA_LDFLAGS)

SRCS     := $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/*.s)
SDK_SRCS := $(wildcard $(SDK_DIR)/src/*.c) $(wildcard $(SDK_DIR)/src/*.s)
X_SRCS   := $(foreach d,$(EXTRA_SRC_DIRS),$(wildcard $(d)/*.c) $(wildcard $(d)/*.s))
OBJS     := $(patsubst $(SRC_DIR)/%,$(OBJDIR)/%.o,$(SRCS)) $(addprefix $(OBJDIR)/x/,$(addsuffix .o,$(notdir $(X_SRCS)))) $(EXTRA_OBJS)
vpath %.c $(EXTRA_SRC_DIRS)
vpath %.s $(EXTRA_SRC_DIRS)
SDK_OBJS := $(patsubst $(SDK_DIR)/src/%,$(OBJDIR)/sdk/%.o,$(SDK_SRCS))
SDK_LIB  := $(BUILD)/lynxsdk.lib
OUT      := $(BUILD)/$(NAME).lnx

# build/flags: the compile flags of the last build. When they change
# (RELEASE=1, DEBUG=1, OPT...) the objects and the SDK library are deleted, so
# everything is rebuilt with the new flags.
FLAGS_STAMP := $(BUILD)/flags
FLAGS_NOW   := $(CFLAGS) | $(ASFLAGS) | $(CC65_TARGET)
$(shell mkdir -p $(BUILD); if [ "$$(cat $(FLAGS_STAMP) 2>/dev/null)" != '$(FLAGS_NOW)' ]; then rm -rf $(OBJDIR) $(BUILD)/lynxsdk.lib $(BUILD)/*.lnx; printf '%s' '$(FLAGS_NOW)' > $(FLAGS_STAMP); fi)

.DEFAULT_GOAL := all
all: $(OUT)
	@echo "built $(OUT) ($$(wc -c < $(OUT) | tr -d ' ') bytes)"

$(OBJDIR) $(OBJDIR)/sdk $(OBJDIR)/x:
	mkdir -p $@

$(SDK_LIB): $(SDK_OBJS)
	rm -f $@
	ar65 a $@ $(SDK_OBJS)

$(OUT): $(OBJS) $(SDK_LIB) $(CFG)
	cl65 $(LDFLAGS) -o $@ $(OBJS) $(SDK_LIB)

# every object depends on all headers: simple and safe for small projects
HDRS := $(wildcard $(SRC_DIR)/*.h) $(foreach d,$(EXTRA_SRC_DIRS),$(wildcard $(d)/*.h)) $(wildcard $(SDK_DIR)/include/*.h) $(wildcard $(SDK_DIR)/src/*.h)

$(OBJDIR)/%.c.o: $(SRC_DIR)/%.c $(HDRS) | $(OBJDIR)
	cl65 $(CFLAGS) -c --listing $(OBJDIR)/$*.lst -o $@ $<

$(OBJDIR)/%.s.o: $(SRC_DIR)/%.s | $(OBJDIR)
	ca65 $(ASFLAGS) -o $@ $<

$(OBJDIR)/x/%.c.o: %.c $(HDRS) | $(OBJDIR)/x
	cl65 $(CFLAGS) -c -o $@ $<

$(OBJDIR)/x/%.s.o: %.s | $(OBJDIR)/x
	ca65 $(ASFLAGS) $(addprefix -I,$(EXTRA_SRC_DIRS)) -o $@ $<

$(OBJDIR)/sdk/%.c.o: $(SDK_DIR)/src/%.c $(HDRS) | $(OBJDIR)/sdk
	cl65 $(CFLAGS) -c -o $@ $<

$(OBJDIR)/sdk/%.s.o: $(SDK_DIR)/src/%.s | $(OBJDIR)/sdk
	ca65 $(ASFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD)

.PHONY: all clean
