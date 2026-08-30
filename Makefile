.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC")
endif
include $(DEVKITPPC)/wii_rules

TARGET := augusta_golf_demo
BUILD := build
SOURCES := source
INCLUDES := source
LIBS := -lwiiuse -lbte -logc -lm
CFLAGS := -g -O2 -Wall -Wextra $(MACHDEP) $(INCLUDE)
CXXFLAGS := $(CFLAGS)
LDFLAGS := -g $(MACHDEP) -Wl,-Map,$(notdir $@).map

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD := $(CC)
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) -I$(CURDIR)/$(BUILD) -I$(LIBOGC_INC)
export LIBPATHS := -L$(LIBOGC_LIB)
.PHONY: all clean package
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
$(BUILD):
	@mkdir -p $@
clean:
	@rm -rf $(BUILD) $(TARGET).dol $(TARGET).elf $(TARGET).map dist
package: all
	@mkdir -p dist/apps/augusta_golf_demo/data
	@cp $(TARGET).dol dist/apps/augusta_golf_demo/boot.dol
	@cp apps/augusta_golf_demo/meta.xml dist/apps/augusta_golf_demo/meta.xml
	@cp apps/augusta_golf_demo/icon.png dist/apps/augusta_golf_demo/icon.png
	@cp -a data/. dist/apps/augusta_golf_demo/data/
else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
