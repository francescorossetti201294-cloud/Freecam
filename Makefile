#---------------------------------------------------------------------------------
# Free Camera for Pokemon Alpha Sapphire - build file
#
# A plugin (.3gx) made from Sango Plugin. It keeps only the start of the plugin
# (lib/src/boot), the hooks (lib/src/core) and the camera (src/).
#
# Usage (from the devkitPro shell, or in the devkitpro/devkitarm Docker image):
#   make            builds freecam.3gx
#   make clean      removes the build output
#
# 3gxtool must be in the PATH (see README.md).
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

export TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

PLUGIN_VERSION  := 1.2.0
PLUGIN_CREATOR  := FreeCamera
PLUGIN_NAME     := FreeCamera

# Pokemon Alpha Sapphire v1.4 (title id 000400000011C500).
GAME_DEFINE     := -DGAME_ORAS

# LTO=1 (the default) also removes the code that nothing uses.
LTO        ?= 1
LTO_FLAGS  := $(if $(filter 1,$(LTO)),-flto=auto)

TARGET     := freecam
BUILD      := build
PSF        := freecam.plgInfo
SOURCES    := lib/src/boot lib/src/core src
INCLUDES   := lib/include lib/src src

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -mword-relocations \
           -ffunction-sections -fdata-sections -fno-strict-aliasing \
           $(ARCH) -DNDEBUG=1 -Os -fomit-frame-pointer $(LTO_FLAGS) \
           -DPLUGIN_CREATOR=\"$(PLUGIN_CREATOR)\" \
           -DPLUGIN_VERSION=\"$(PLUGIN_VERSION)\" \
           -DPLUGIN_NAME=\"$(PLUGIN_NAME)\" \
           -DPRODUCT_DATA_FOLDER=\"freecam\" \
           -DUSE_SANGO_PLUGIN $(GAME_DEFINE)

CFLAGS  += $(INCLUDE) -D__3DS__

# The plugin runs inside the memory of the game: no exceptions, no RTTI.
CXX_ONLY := -fno-rtti -fno-exceptions -fno-threadsafe-statics -fcheck-new \
            -std=gnu++11
CXXFLAGS := $(CFLAGS) $(CXX_ONLY)

ASFLAGS  := $(ARCH)
# With LTO, the link compiles the code: it needs the same options.
LDFLAGS  := -T $(TOPDIR)/3gx.ld $(ARCH) -DNDEBUG=1 -Os -fomit-frame-pointer \
            $(LTO_FLAGS) $(CXX_ONLY) \
            -Wl,--strip-discarded,--strip-debug,--gc-sections,--section-start,.text=0x07000100

LIBS     := -lctru -lm
LIBDIRS  := $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CCFILES  := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cc)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD      := $(CXX)
export OFILES  := $(CCFILES:.cc=.o) $(SFILES:.s=.o)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L $(dir)/lib)

export PSF

.PHONY: all clean

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).3gx

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

.DEFAULT_GOAL := $(OUTPUT).3gx

$(OUTPUT).3gx : $(OUTPUT).elf
$(OUTPUT).elf : $(OFILES)

#---------------------------------------------------------------------------------
%.o: %.cc
	@echo $(notdir $<)
	$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

#---------------------------------------------------------------------------------
%.3gx: %.elf
	@echo creating $(notdir $@)
	@3gxtool -s $^ $(TOPDIR)/$(PSF) $@

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
