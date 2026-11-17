# The GPU stereo test (docs/3DS_GPU_TEST.md).  From this folder:
#   make          -> OpenSWOSGpuTest.3dsx  (Homebrew Launcher, or 3dslink)
#   make cia      -> OpenSWOSGpuTest.cia   (needs makerom + bannertool; installs beside the game)
# Needs devkitPro with 3ds-dev (libctru, citro3d, citro2d).
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO to your devkitPro install (export DEVKITPRO=/opt/devkitpro)")
endif
DEVKITARM ?= $(DEVKITPRO)/devkitARM
CTRULIB   := $(DEVKITPRO)/libctru
TOOLS     := $(DEVKITPRO)/tools/bin
CC        := $(DEVKITARM)/bin/arm-none-eabi-gcc

TARGET  := OpenSWOSGpuTest
OBJDIR  := build
ELF     := $(OBJDIR)/$(TARGET).elf

APP_TITLE       := OpenSWOS GPU test
APP_DESCRIPTION := GPU stereo rendering test
APP_AUTHOR      := OpenSWOS 3D
APP_ICON        := ../../packaging/3ds/icon.png
APP_BANNER      := ../../packaging/3ds/banner.png
APP_BANNER_WAV  := ../../packaging/3ds/banner.wav
APP_RSF         := OpenSWOSGpuTest.rsf
MAKEROM         ?= makerom
BANNERTOOL      ?= bannertool

ARCH   := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := $(ARCH) -O2 -g -std=gnu11 -Wall -ffunction-sections -fdata-sections -D__3DS__ -I$(CTRULIB)/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections -L$(CTRULIB)/lib
LDLIBS  := -lcitro2d -lcitro3d -lctru -lm

.PHONY: all elf cia clean
all: $(TARGET).3dsx
elf: $(ELF)

$(OBJDIR):
	@mkdir -p $@

$(OBJDIR)/main.o: main.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJDIR)/main.o
	$(CC) $(LDFLAGS) $< $(LDLIBS) -o $@

$(OBJDIR)/$(TARGET).smdh: $(APP_ICON) | $(OBJDIR)
	$(TOOLS)/smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(APP_ICON) $@

$(TARGET).3dsx: $(ELF) $(OBJDIR)/$(TARGET).smdh
	$(TOOLS)/3dsxtool $(ELF) $@ --smdh=$(OBJDIR)/$(TARGET).smdh

$(OBJDIR)/$(TARGET).bnr: $(APP_BANNER) $(APP_BANNER_WAV) | $(OBJDIR)
	$(BANNERTOOL) makebanner -o $@ -i $(APP_BANNER) -a $(APP_BANNER_WAV)

cia: $(TARGET).cia
$(TARGET).cia: $(ELF) $(APP_RSF) $(OBJDIR)/$(TARGET).smdh $(OBJDIR)/$(TARGET).bnr
	$(MAKEROM) -f cia -target t -exefslogo -o $@ -elf $(ELF) -rsf $(APP_RSF) \
	    -banner $(OBJDIR)/$(TARGET).bnr -icon $(OBJDIR)/$(TARGET).smdh

clean:
	rm -rf $(OBJDIR) $(TARGET).3dsx $(TARGET).cia
