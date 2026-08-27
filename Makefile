ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC (install the devkitPro wii-dev group)")
endif
include $(DEVKITPPC)/wii_rules
TARGET := augusta_golf
BUILD := build
SOURCES := source
DATA := data
INCLUDES := source
LIBS := -lwiiuse -lbte -logc -lm
CFLAGS := -g -O2 -Wall -Wextra $(MACHDEP) $(INCLUDE)
LDFLAGS := -g $(MACHDEP) -Wl,-Map,$(notdir $@).map
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir))
.PHONY: all clean package
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
$(BUILD):
	@mkdir -p $@
package: all
	@mkdir -p dist/apps/augusta_golf
	@cp $(TARGET).dol dist/apps/augusta_golf/boot.dol
	@cp apps/augusta_golf/meta.xml dist/apps/augusta_golf/meta.xml
	@if [ -f apps/augusta_golf/icon.png ]; then cp apps/augusta_golf/icon.png dist/apps/augusta_golf/icon.png; fi
	@mkdir -p dist/apps/augusta_golf/data
	@if [ -d data ]; then cp -a data/. dist/apps/augusta_golf/data/; fi
clean:
	@rm -rf $(BUILD) $(TARGET).dol $(TARGET).elf $(TARGET).map dist
else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
