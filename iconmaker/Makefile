#---------------------------------------------------------------------------------
# Icon Maker for gpSP 3D. Build with ./build-iconmaker.sh (it makes the
# template forwarder in data/ first).
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET   := iconmaker
BUILD    := build
SOURCES  := source
DATA     := data
INCLUDES := source ../gpsp3ds/source
# gpSP 3D's UI kit (ui.c, ui_font.c), shared so both apps look the same
UIKIT    := ../gpsp3ds/source

APP_TITLE       := Icon Maker
APP_DESCRIPTION := GBA + SNES games on the HOME Menu
APP_AUTHOR      := bheppe
ICON            := res/icon.png

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) $(INCLUDE) -D__3DS__
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lctru -lm
LIBDIRS  := $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                  $(foreach dir,$(DATA),$(CURDIR)/$(dir)) $(CURDIR)/$(UIKIT)
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))) ui.c ui_font.c
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))
export LD          := $(CC)
export OFILES_BIN  := $(addsuffix .o,$(BINFILES))
export OFILES_SRC  := $(CFILES:.c=.o)
export OFILES      := $(OFILES_BIN) $(OFILES_SRC)
export HFILES      := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE     := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                      $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS    := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_ICON    := $(TOPDIR)/$(ICON)
export _3DSXFLAGS  += --smdh=$(CURDIR)/$(TARGET).smdh
.PHONY: $(BUILD) clean all
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf
else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).3dsx : $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf  : $(OFILES)
$(OFILES_SRC)  : $(HFILES)
%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)
-include $(DEPENDS)
endif
