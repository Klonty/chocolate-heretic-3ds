#---------------------------------------------------------------------------------
# Chocolate Heretic for the Nintendo 3DS
#
# Run "make" from this directory with devkitPro installed.  Produces
# heretic.3dsx (and heretic.elf/heretic.smdh) in this directory.
#
# "make cia" also builds heretic.cia for installing to the HOME menu; it
# needs makerom and bannertool on the PATH.  The version shown on the 3DS
# comes from VERSION_MAJOR/MINOR/MICRO (e.g. make cia VERSION_MINOR=4).
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)

# HOME menu / Homebrew Launcher art (drawn by meta/make_art.py).
APP_ICON    := $(TOPDIR)/meta/icon.png

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

# Bundled third-party code for the Blasphemer download (p3ds_getwad.c):
# mbedtls for HTTPS and zlib's inflate for unzipping.  See
# port3ds/third_party/README.md.
TP          := $(ROOT)/port3ds/third_party
MBEDTLS_C   := $(wildcard $(TP)/mbedtls/library/*.c)
ZLIB_C      := $(addprefix $(TP)/zlib/,adler32.c crc32.c inffast.c \
               inflate.c inftrees.c zutil.c)

ALL_C       := $(SRC_C) $(HERETIC_C) $(OPL_C) $(PORT_C) $(MBEDTLS_C) $(ZLIB_C)

# Art for the first-start screen, drawn by meta/make_art.py.
BINFILES    := getwad_top.bin getwad_font.bin

INCLUDES    := port3ds/include port3ds src src/heretic opl \
               port3ds/third_party port3ds/third_party/mbedtls/include \
               port3ds/third_party/zlib

ARCH        := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS      := -g -Wall -O2 -mword-relocations \
               -ffunction-sections -fdata-sections \
               $(ARCH) \
               -D__3DS__ -DARM11 \
               -DMBEDTLS_CONFIG_FILE='"mbedtls_config_3ds.h"'

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
export VPATH    := $(sort $(dir $(ALL_C))) $(CURDIR)/meta
export DEPSDIR  := $(CURDIR)/$(BUILD)

export OFILES_SOURCES := $(notdir $(ALL_C:.c=.o))
export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES   := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES   := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export LD       := $(CC)
export INCLUDE  := $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXFLAGS := --smdh=$(OUTPUT).smdh

.PHONY: $(BUILD) clean all cia

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

VERSION_MAJOR ?= 0
VERSION_MINOR ?= 0
VERSION_MICRO ?= 0

cia: $(BUILD)
	@sh meta/make_cia.sh $(TARGET).elf $(TARGET).cia "$(APP_TITLE)" \
		"$(APP_DESCRIPTION)" "$(APP_AUTHOR)" \
		$(VERSION_MAJOR) $(VERSION_MINOR) $(VERSION_MICRO)

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia

#---------------------------------------------------------------------------------
else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx : $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf  : $(OFILES)

# Sources may include the generated *_bin.h headers.
$(OFILES_SOURCES) : $(HFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
