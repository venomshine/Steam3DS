#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Steam3DS — cliente ligero de la tienda de Steam para 3DS
#---------------------------------------------------------------------------------
APP_TITLE       := Steam3DS
APP_DESCRIPTION := Tienda de Steam en tu 3DS
APP_AUTHOR      := VENOMSHiNE
ICON            := meta/icon.png

TARGET      := Steam3DS
BUILD       := build
SOURCES     := source lib
DATA        := data
INCLUDES    := source lib
GRAPHICS    :=
ROMFS       := romfs

#---------------------------------------------------------------------------------
# DEV=1 compila una versión de pruebas que pasa las peticiones por un relé local
# (solo para el emulador en la nube; nunca se publica).
#---------------------------------------------------------------------------------
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations \
           -ffunction-sections \
           $(ARCH)

CFLAGS  += $(INCLUDE) -D__3DS__
ifeq ($(DEV),1)
CFLAGS  += -DDEV_RELAY
endif

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,--gc-sections -Wl,--wrap=fontGlyphIndexFromCodePoint

LIBS    := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -ljpeg -lcitro2d -lcitro3d -lctru -lm

LIBDIRS := $(PORTLIBS) $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD   := $(CC)

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES         := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)

export APP_ICON := $(TOPDIR)/$(ICON)

ifeq ($(strip $(NO_SMDH)),)
    export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
    export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Sprites de la pantalla de carga (tools/gen_art.py y el flipbook de vapor de tools/gen_steam.py)
$(ROMFS)/gfx/sprites.t3x: gfx/sprites.t3s $(wildcard gfx/*.png) $(wildcard gfx/steam/*.png)
	@mkdir -p $(ROMFS)/gfx
	@cd gfx && $(DEVKITPRO)/tools/bin/tex3ds -i sprites.t3s -o ../$@
	@echo built ... $(notdir $@)

# Fondos de la pantalla de carga, sin alfa (RGB565: la mitad de memoria)
$(ROMFS)/gfx/splash_bg.t3x: gfx/splash_bg.t3s gfx/bg_top.png gfx/bg_bot.png
	@mkdir -p $(ROMFS)/gfx
	@cd gfx && $(DEVKITPRO)/tools/bin/tex3ds -i splash_bg.t3s -o ../$@
	@echo built ... $(notdir $@)

# La válvula 3D de la pantalla de carga (tools/gen_wheel3d.py), en su propia hoja: no cabe con lo demás
$(ROMFS)/gfx/wheel3d.t3x: gfx/wheel3d.t3s gfx/wheel_shadow.png $(wildcard gfx/w3d/*.png)
	@mkdir -p $(ROMFS)/gfx
	@cd gfx && $(DEVKITPRO)/tools/bin/tex3ds -i wheel3d.t3s -o ../$@
	@echo built ... $(notdir $@)

$(BUILD): $(ROMFS)/gfx/sprites.t3x $(ROMFS)/gfx/splash_bg.t3x $(ROMFS)/gfx/wheel3d.t3x
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(ROMFS)/gfx

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

$(OUTPUT).3dsx : $(OUTPUT).elf $(_3DSXDEPS)
$(OFILES_SOURCES) : $(HFILES)
$(OUTPUT).elf : $(OFILES)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
