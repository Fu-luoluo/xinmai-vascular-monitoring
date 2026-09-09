# Central_LCD_JumpIAP — 4KB EEPROM-based boot selector @ 0x0000

ROOT     := ..
LF       := $(ROOT)/LinkFiles
PROJ     := $(ROOT)/Central_LCD_JumpIAP

CC       := riscv-wch-elf-gcc
OBJCOPY  := riscv-wch-elf-objcopy
SIZE     := riscv-wch-elf-size

CFLAGS   := -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany \
            -msmall-data-limit=8 -mno-save-restore -Os -g \
            -ffunction-sections -fdata-sections -fno-common -std=gnu99 \
            -DFREQ_SYS=62400000 \
            -I$(LF)/StdPeriphDriver/inc -I$(LF)/RVMSIS

LDFLAGS  := -T $(PROJ)/Ld/Link.ld -nostartfiles -Wl,--gc-sections \
            -L$(LF)/StdPeriphDriver \
            --specs=nano.specs --specs=nosys.specs

SRCS     := $(PROJ)/APP/jump_main.c \
            $(PROJ)/Startup/startup_jump_select.S \
            $(LF)/StdPeriphDriver/CH58x_sys.c
OBJS     := $(PROJ)/obj/jump_main.o \
            $(PROJ)/obj/startup_jump_select.o \
            $(PROJ)/obj/CH58x_sys.o

.PHONY: all clean dirs

all: dirs $(PROJ)/obj/Central_LCD_JumpIAP.hex
	$(SIZE) $(PROJ)/obj/Central_LCD_JumpIAP.elf

ifeq ($(OS),Windows_NT)
dirs:
	@if not exist "$(PROJ)/obj" mkdir "$(PROJ)/obj"
else
dirs:
	@mkdir -p $(PROJ)/obj
endif

$(PROJ)/obj/Central_LCD_JumpIAP.hex: $(PROJ)/obj/Central_LCD_JumpIAP.elf
	$(OBJCOPY) -O ihex $< $@

$(PROJ)/obj/Central_LCD_JumpIAP.elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lISP585

$(PROJ)/obj/jump_main.o: $(PROJ)/APP/jump_main.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/startup_jump_select.o: $(PROJ)/Startup/startup_jump_select.S
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/CH58x_sys.o: $(LF)/StdPeriphDriver/CH58x_sys.c
	$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(OS),Windows_NT)
clean:
	@if exist "$(PROJ)/obj" rmdir /s /q "$(PROJ)/obj"
else
clean:
	rm -rf $(PROJ)/obj
endif
