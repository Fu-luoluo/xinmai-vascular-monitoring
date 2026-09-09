# Central_LCD_IAP — 12KB IAP @ 0x6D000, copies W25Q OTA image to App

ROOT     := ..
LF       := $(ROOT)/LinkFiles
LCD      := $(ROOT)/Central_LCD
PROJ     := $(ROOT)/Central_LCD_IAP
W25Q     := $(LCD)/Drivers/BSP/W25Q

CC       := riscv-wch-elf-gcc
OBJCOPY  := riscv-wch-elf-objcopy
SIZE     := riscv-wch-elf-size

INCLUDES := -I$(LF)/StdPeriphDriver/inc \
            -I$(LF)/RVMSIS \
            -I$(LCD)/APP/include \
            -I$(W25Q) \
            -I$(LF)/HAL/include \
            -I$(LF)/LIB \
            -I$(LF)/Startup

CFLAGS   := -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany \
            -msmall-data-limit=8 -mno-save-restore -Os -g \
            -ffunction-sections -fdata-sections -fno-common -std=gnu99 \
            -DOTA_STAGE1_TEST=0 -DBLE_SNV_ADDR=0x7100 -DFREQ_SYS=62400000 \
            --param=highcode-gen-section-name=1 $(INCLUDES)

LDFLAGS  := -T $(PROJ)/Ld/Link.ld -nostartfiles -Wl,--gc-sections \
            -L$(LF)/StdPeriphDriver -L$(LF)/LIB \
            --specs=nano.specs --specs=nosys.specs

APP_SRCS := $(PROJ)/APP/iap_main.c \
            $(LCD)/APP/ota_flag.c \
            $(LCD)/APP/ota_store.c \
            $(W25Q)/flash_store.c \
            $(W25Q)/w25qxx.c \
            $(W25Q)/w25q_spi.c

SYS_SRCS := $(LF)/Startup/startup_CH585.S \
            $(LF)/StdPeriphDriver/CH58x_sys.c \
            $(LF)/StdPeriphDriver/CH58x_clk.c \
            $(LF)/StdPeriphDriver/CH58x_gpio.c \
            $(LF)/StdPeriphDriver/CH58x_spi0.c \
            $(LF)/StdPeriphDriver/CH58x_uart0.c

ALL_SRCS := $(APP_SRCS) $(SYS_SRCS)

OBJS     := $(addprefix $(PROJ)/obj/,$(notdir $(ALL_SRCS:%.c=%.o)))
OBJS     := $(OBJS:%.S=%.o)

.PHONY: all clean dirs

all: dirs $(PROJ)/obj/Central_LCD_IAP.hex
	$(SIZE) $(PROJ)/obj/Central_LCD_IAP.elf

ifeq ($(OS),Windows_NT)
dirs:
	@if not exist "$(PROJ)/obj" mkdir "$(PROJ)/obj"
else
dirs:
	@mkdir -p $(PROJ)/obj
endif

$(PROJ)/obj/Central_LCD_IAP.hex: $(PROJ)/obj/Central_LCD_IAP.elf
	$(OBJCOPY) -O ihex $< $@

$(PROJ)/obj/Central_LCD_IAP.elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lISP585

$(PROJ)/obj/iap_main.o: $(PROJ)/APP/iap_main.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/ota_flag.o: $(LCD)/APP/ota_flag.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/ota_store.o: $(LCD)/APP/ota_store.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/flash_store.o: $(W25Q)/flash_store.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/w25qxx.o: $(W25Q)/w25qxx.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/w25q_spi.o: $(W25Q)/w25q_spi.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/startup_CH585.o: $(LF)/Startup/startup_CH585.S
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/CH58x_sys.o: $(LF)/StdPeriphDriver/CH58x_sys.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/CH58x_clk.o: $(LF)/StdPeriphDriver/CH58x_clk.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/CH58x_gpio.o: $(LF)/StdPeriphDriver/CH58x_gpio.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/CH58x_spi0.o: $(LF)/StdPeriphDriver/CH58x_spi0.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROJ)/obj/CH58x_uart0.o: $(LF)/StdPeriphDriver/CH58x_uart0.c
	$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(OS),Windows_NT)
clean:
	@if exist "$(PROJ)/obj" rmdir /s /q "$(PROJ)/obj"
else
clean:
	rm -rf $(PROJ)/obj
endif
