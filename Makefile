#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/gba_rules

#---------------------------------------------------------------------------------
TARGET		:= game
BUILD		:= build
SOURCES		:= src
INCLUDES	:= include
DATA		:=
SONGS_DIR	:= songs
#---------------------------------------------------------------------------------

ARCH	:=	-mthumb -mthumb-interwork

CFLAGS	:=	-g -Wall -O3\
		-mcpu=arm7tdmi -mtune=arm7tdmi\
 		-fomit-frame-pointer\
		-ffast-math \
		$(ARCH)

CFLAGS	 +=	$(INCLUDE)

CXXFLAGS :=	$(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	 :=	-g $(ARCH)
LDFLAGS	  =	-g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lmm -lgba
LIBDIRS	:=	$(LIBGBA)

ifneq ($(BUILDDIR), $(CURDIR))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

# Tool to generate playlist.c from MIDIs
PLAYLIST_C := $(CURDIR)/src/playlist.c
$(PLAYLIST_C): $(wildcard $(SONGS_DIR)/*.mid) $(CURDIR)/tools/midi_to_chart.py
	@echo Generating playlist...
	@python3 $(CURDIR)/tools/midi_to_chart.py $(CURDIR)/$(SONGS_DIR) $(PLAYLIST_C)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
# Ensure playlist.c is in the list even if not generated yet
ifeq ($(findstring playlist.c,$(CFILES)),)
	CFILES += playlist.c
endif

CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES         := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD)
 
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean
 
$(BUILD): $(PLAYLIST_C)
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) BUILDDIR=`cd $(BUILD) && pwd` --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -rf *.elf *.gba
	@rm -rf build
	@rm -f src/playlist.c
 
else
 
$(OUTPUT).gba	:	$(OUTPUT).elf
$(OUTPUT).elf	:	$(OFILES)
$(OFILES_SOURCES) : $(HFILES)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d
 
endif
