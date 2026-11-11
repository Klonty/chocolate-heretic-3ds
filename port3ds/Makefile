#---------------------------------------------------------------------------------
# Chocolate Heretic for the Nintendo 3DS
#
# Run "make" from this directory with devkitPro installed.  Produces
# heretic.3dsx (and heretic.elf/heretic.smdh) in this directory.
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := heretic
BUILD       := build
ROOT        := $(TOPDIR)/..
APP_TITLE   := Heretic
APP_DESCRIPTION := Chocolate Heretic for 3DS
APP_AUTHOR  := Chocolate Doom / 3DS port

# Game-independent engine sources (src/) with the SDL-based platform
# files taken out; they are replaced by the files in port3ds/.
SRC_EXCLUDE := \
	i_video.c i_videohr.c i_input.c i_joystick.c \
	i_sdlsound.c i_sdlmusic.c i_flmusic.c i_winmusic.c i_pcsound.c \
	i_endoom.c net_gui.c d_dedicated.c w_file_win32.c z_native.c

SRC_C       := $(filter-out $(addprefix $(ROOT)/src/,$(SRC_EXCLUDE)),$(wildcard $(ROOT)/src/*.c))
HERETIC_C   := $(filter-out $(ROOT)/src/heretic/heretic_icon.c,$(wildcard $(ROOT)/src/heretic/*.c))
OPL_C       := $(ROOT)/opl/opl.c $(ROOT)/opl/opl_queue.c
PORT_C      := $(wildcard $(ROOT)/port3ds/*.c)

ALL_C       := $(SRC_C) $(HERETIC_C) $(OPL_C) $(PORT_C)

INCLUDES    := port3ds/include port3ds src src/heretic opl

ARCH        := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS      := -g -Wall -O2 -mword-relocations \
               -ffunction-sections -fdata-sections \
               $(ARCH) \
               -D__3DS__ -DARM11

CFLAGS      += $(foreach dir,$(INCLUDES),-I$(ROOT)/$(dir)) \
               -I$(TOPDIR)/$(BUILD) -I$(CTRULIB)/include

CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS     := -g $(ARCH)
LDFLAGS      = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS        := -lctru -lm
LIBDIRS     := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(sort $(dir $(ALL_C)))
export DEPSDIR  := $(CURDIR)/$(BUILD)

export OFILES   := $(notdir $(ALL_C:.c=.o))
export LD       := $(CC)
export INCLUDE  := $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXFLAGS := --smdh=$(OUTPUT).smdh

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx : $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf  : $(OFILES)

-include $(DEPENDS)

endif
