#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

export TARGET := finlink-nds
export TOPDIR := $(CURDIR)

# Leave blank to use ds_rules' built-in default (calico/share/nds-icon.bmp).
GAME_ICON :=

NITRO_FILES :=

GAME_TITLE     := finlink
GAME_SUBTITLE1 := GBA-Stream (Machbarkeitstest)
GAME_SUBTITLE2 := github.com/alwe2710/finlink

include $(DEVKITARM)/ds_rules

.PHONY: checkarm7 checkarm9 clean

#---------------------------------------------------------------------------------
# Two separately-built ELFs (ARM9 main program + ARM7 "default core", see
# arm7/source/template.c) combined via ndstool -- unlike the other three
# clients' CMake builds, devkitPro's own NDS.cmake has no helper for this
# two-CPU combination step (confirmed by grepping it), and even
# devkitPro's official example for this exact case (templates/combined)
# uses this same classic Makefile structure instead of CMake. Verified
# end-to-end against that reference example before adapting it here.
#---------------------------------------------------------------------------------
all: checkarm7 checkarm9 $(TARGET).nds

#---------------------------------------------------------------------------------
checkarm7:
	$(MAKE) -C arm7

#---------------------------------------------------------------------------------
checkarm9:
	$(MAKE) -C arm9

#---------------------------------------------------------------------------------
$(TARGET).nds : $(NITRO_FILES) arm7/$(TARGET).elf arm9/$(TARGET).elf
	ndstool	-c $(TARGET).nds -7 arm7/$(TARGET).elf -9 arm9/$(TARGET).elf \
	-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)" \
	$(_ADDFILES)

#---------------------------------------------------------------------------------
arm7/$(TARGET).elf:
	$(MAKE) -C arm7

#---------------------------------------------------------------------------------
arm9/$(TARGET).elf:
	$(MAKE) -C arm9

#---------------------------------------------------------------------------------
clean:
	$(MAKE) -C arm9 clean
	$(MAKE) -C arm7 clean
	rm -f $(TARGET).nds
