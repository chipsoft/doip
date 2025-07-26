################################################################################
# SAME54 Embedded Project Makefile
# Moved to root directory for better build management
################################################################################

ifdef SystemRoot
	SHELL = cmd.exe
	MK_DIR = mkdir
else
	ifeq ($(shell uname), Linux)
		MK_DIR = mkdir -p
	endif

	ifeq ($(shell uname | cut -d _ -f 1), CYGWIN)
		MK_DIR = mkdir -p
	endif

	ifeq ($(shell uname | cut -d _ -f 1), MINGW32)
		MK_DIR = mkdir -p
	endif

	ifeq ($(shell uname | cut -d _ -f 1), MINGW64)
		MK_DIR = mkdir -p
	endif

	ifeq ($(shell uname | cut -d _ -f 1), DARWIN)
		MK_DIR = mkdir -p
	endif
endif

# Build directory
BUILD_DIR = build

# List the subdirectories for creating object files
SUB_DIRS += \
$(BUILD_DIR) \
$(BUILD_DIR)/app_libs/asf4/hpl/ramecc \
$(BUILD_DIR)/app_libs/asf4/hpl/gmac \
$(BUILD_DIR)/gcc/gcc \
$(BUILD_DIR)/ethernet_phy \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/portable/GCC/ARM_CM4F \
$(BUILD_DIR)/examples \
$(BUILD_DIR)/app_libs/asf4/hpl/oscctrl \
$(BUILD_DIR)/app_libs/asf4/hpl/osc32kctrl \
$(BUILD_DIR)/app_libs/lwip/src/api \
$(BUILD_DIR)/app_libs/lwip/src/core \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv4 \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv6 \
$(BUILD_DIR)/app_libs/lwip/src/netif \
$(BUILD_DIR)/app_libs/lwip/contrib/ports/freertos \
$(BUILD_DIR)/app_libs/lwip/port \
$(BUILD_DIR)/app_libs/asf4/hpl/dmac \
$(BUILD_DIR)/app_libs/asf4/hal/src \
$(BUILD_DIR)/app_libs/asf4/hal/utils/src \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/portable/MemMang \
$(BUILD_DIR)/app_libs/asf4/hpl/pm \
$(BUILD_DIR)/app_libs/asf4/hpl/cmcc \
$(BUILD_DIR)/app_libs/asf4/hpl/gclk \
$(BUILD_DIR)/app_libs/asf4/hpl/mclk \
$(BUILD_DIR)/app_libs/asf4/hpl/sercom \
$(BUILD_DIR)/app_libs/asf4/hpl/core \
$(BUILD_DIR)/app_libs/SEGGER_RTT_V794b/RTT \
$(BUILD_DIR)/app_libs/printf \
$(BUILD_DIR)/drivers \
$(BUILD_DIR)/hw/same54/drivers

# List the object files
OBJS += \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/queue.o \
$(BUILD_DIR)/app_libs/asf4/hal/utils/src/utils_syscalls.o \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv4/icmp.o \
$(BUILD_DIR)/app_libs/lwip/src/core/def.o \
$(BUILD_DIR)/app_libs/asf4/hpl/pm/hpl_pm.o \
$(BUILD_DIR)/app_libs/lwip/src/api/netbuf.o \
$(BUILD_DIR)/ethernet_phy_main.o \
$(BUILD_DIR)/app_libs/lwip/src/core/sys.o \
$(BUILD_DIR)/app_libs/asf4/hal/src/hal_usart_sync.o \
$(BUILD_DIR)/app_libs/asf4/hpl/gclk/hpl_gclk.o \
$(BUILD_DIR)/ethernet_phy/ethernet_phy.o \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv4/autoip.o \
$(BUILD_DIR)/app_libs/asf4/hal/src/hal_gpio.o \
$(BUILD_DIR)/app_libs/asf4/startup_same54.o \
$(BUILD_DIR)/app_libs/lwip/src/core/timeouts.o \
$(BUILD_DIR)/app_libs/lwip/src/api/err.o \
$(BUILD_DIR)/app_libs/lwip/src/api/api_msg.o \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/portable/MemMang/heap_2.o \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/list.o \
$(BUILD_DIR)/app_libs/lwip/src/core/tcp_out.o \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv4/ip4_frag.o \
$(BUILD_DIR)/app_libs/lwip/src/core/pbuf.o \
$(BUILD_DIR)/app_libs/lwip/src/core/tcp_in.o \
$(BUILD_DIR)/app_libs/lwip/src/core/udp.o \
$(BUILD_DIR)/app_libs/asf4/hal/utils/src/utils_list.o \
$(BUILD_DIR)/app_libs/asf4/hpl/dmac/hpl_dmac.o \
$(BUILD_DIR)/app_libs/asf4/hpl/osc32kctrl/hpl_osc32kctrl.o \
$(BUILD_DIR)/app_libs/lwip/src/api/netdb.o \
$(BUILD_DIR)/driver_init.o \
$(BUILD_DIR)/main.o \
$(BUILD_DIR)/app_libs/lwip/src/core/memp.o \
$(BUILD_DIR)/app_libs/asf4/hal/utils/src/utils_event.o \
$(BUILD_DIR)/app_libs/asf4/hal/src/hal_mac_async.o \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/croutine.o \
$(BUILD_DIR)/app_libs/asf4/hpl/cmcc/hpl_cmcc.o \
$(BUILD_DIR)/atmel_start.o \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv4/etharp.o \
$(BUILD_DIR)/app_libs/asf4/hal/src/hal_atomic.o \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv4/dhcp.o \
$(BUILD_DIR)/app_libs/asf4/hal/src/hal_io.o \
$(BUILD_DIR)/app_libs/lwip/src/core/raw.o \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv4/ip4.o \
$(BUILD_DIR)/app_libs/lwip/src/core/mem.o \
$(BUILD_DIR)/app_libs/asf4/hpl/core/hpl_core_m4.o \
$(BUILD_DIR)/app_libs/lwip/src/core/tcp.o \
$(BUILD_DIR)/eth_ipstack_main.o \
$(BUILD_DIR)/app_libs/asf4/hal/src/hal_delay.o \
$(BUILD_DIR)/app_libs/asf4/hpl/core/hpl_init.o \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/event_groups.o \
$(BUILD_DIR)/app_libs/asf4/hpl/oscctrl/hpl_oscctrl.o \
$(BUILD_DIR)/app_libs/lwip/src/netif/slipif.o \
$(BUILD_DIR)/app_libs/asf4/hal/src/hal_init.o \
$(BUILD_DIR)/app_libs/lwip/contrib/ports/freertos/sys_arch.o \
$(BUILD_DIR)/app_libs/asf4/hpl/sercom/hpl_sercom.o \
$(BUILD_DIR)/webserver_tasks.o \
$(BUILD_DIR)/app_libs/asf4/hal/src/hal_sleep.o \
$(BUILD_DIR)/app_libs/lwip/src/core/init.o \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/timers.o \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/stream_buffer.o \
$(BUILD_DIR)/app_libs/lwip/src/core/inet_chksum.o \
$(BUILD_DIR)/app_libs/lwip/src/core/ip.o \
$(BUILD_DIR)/app_libs/lwip/src/core/dns.o \
$(BUILD_DIR)/lwip_socket_api.o \
$(BUILD_DIR)/app_libs/asf4/hpl/gmac/hpl_gmac.o \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv4/igmp.o \
$(BUILD_DIR)/app_libs/lwip/src/core/stats.o \
$(BUILD_DIR)/app_libs/asf4/hpl/ramecc/hpl_ramecc.o \
$(BUILD_DIR)/app_libs/asf4/hal/utils/src/utils_assert.o \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv4/ip4_addr.o \
$(BUILD_DIR)/app_libs/lwip/src/core/ipv4/acd.o \
$(BUILD_DIR)/app_libs/lwip/src/netif/ethernet.o \
$(BUILD_DIR)/app_libs/asf4/hpl/mclk/hpl_mclk.o \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/tasks.o \
$(BUILD_DIR)/app_libs/lwip/src/api/netifapi.o \
$(BUILD_DIR)/app_libs/lwip/src/api/sockets.o \
$(BUILD_DIR)/app_libs/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.o \
$(BUILD_DIR)/app_libs/lwip/src/core/netif.o \
$(BUILD_DIR)/app_libs/asf4/hal/src/hal_cache.o \
$(BUILD_DIR)/app_libs/lwip/src/api/tcpip.o \
$(BUILD_DIR)/app_libs/SEGGER_RTT_V794b/RTT/SEGGER_RTT.o \
$(BUILD_DIR)/app_libs/printf/printf.o \
$(BUILD_DIR)/rtt_printf.o \
$(BUILD_DIR)/network_events.o \
$(BUILD_DIR)/doip_client.o \
$(BUILD_DIR)/app_libs/lwip/src/api/api_lib.o \
$(BUILD_DIR)/app_libs/lwip/port/ethif_mac.o \
$(BUILD_DIR)/drivers/driver_led.o \
$(BUILD_DIR)/hw/same54/drivers/bsp_led.o \
$(BUILD_DIR)/drivers/driver_ethernet.o \
$(BUILD_DIR)/hw/same54/drivers/bsp_ethernet.o \

# List the directories containing header files
DIR_INCLUDES += \
-I"." \
-I"config" \
-I"examples" \
-I"app_libs/asf4/hal/include" \
-I"app_libs/asf4/hal/utils/include" \
-I"app_libs/asf4/hpl/cmcc" \
-I"app_libs/asf4/hpl/core" \
-I"app_libs/asf4/hpl/dmac" \
-I"app_libs/asf4/hpl/gclk" \
-I"app_libs/asf4/hpl/mclk" \
-I"app_libs/asf4/hpl/osc32kctrl" \
-I"app_libs/asf4/hpl/oscctrl" \
-I"app_libs/asf4/hpl/pm" \
-I"app_libs/asf4/hpl/port" \
-I"app_libs/asf4/hpl/ramecc" \
-I"app_libs/asf4/hpl/sercom" \
-I"app_libs/asf4/hri" \
-I"config" \
-I"thirdparty/RTOS" \
-I"app_libs/FreeRTOS-Kernel/include" \
-I"thirdparty/RTOS" \
-I"app_libs/FreeRTOS-Kernel/portable/GCC/ARM_CM4F" \
-I"app_libs/FreeRTOS-Kernel/portable/MemMang" \
-I"app_libs/lwip/src/include" \
-I"app_libs/lwip/contrib/ports/freertos/include" \
-I"app_libs/lwip/port" \
-I"app_libs/lwip/port/include" \
-I"ethernet_phy" \
-I"CMSIS/Core/Include" \
-I"include" \
-I"app_libs/SEGGER_RTT_V794b/RTT" \
-I"app_libs/SEGGER_RTT_V794b/Config" \
-I"app_libs/printf" \
-I"drivers" \
-I"hw/same54/drivers"

# List the dependency files
DEPS := $(OBJS:%.o=%.d)

OUTPUT_FILE_NAME := AtmelStart
QUOTE := "
OUTPUT_FILE_PATH := $(BUILD_DIR)/$(OUTPUT_FILE_NAME).elf

vpath %.c .
vpath %.s .
vpath %.S .

# Phony targets
.PHONY: all clean distclean rebuild size help

# All Target
all: $(SUB_DIRS) $(OUTPUT_FILE_PATH)
	@echo "Build completed successfully!"
	@echo "Output files in $(BUILD_DIR)/"

# Help target
help:
	@echo "Available targets:"
	@echo "  all       - Build the project (default)"
	@echo "  clean     - Remove build directory"
	@echo "  distclean - Remove all generated files"
	@echo "  rebuild   - Clean and build"
	@echo "  size      - Show memory usage"
	@echo "  help      - Show this help message"

# Size target
size: $(OUTPUT_FILE_PATH)
	@echo "Memory usage:"
	"arm-none-eabi-size" "$(OUTPUT_FILE_PATH)"

# Rebuild target
rebuild: clean all

# Linker target
$(OUTPUT_FILE_PATH): $(OBJS)
	@echo "Linking target: $@"
	@echo "Invoking: ARM/GNU Linker"
	$(QUOTE)arm-none-eabi-gcc$(QUOTE) -o $(OUTPUT_FILE_PATH) $(OBJS) -Wl,--start-group -lm -Wl,--end-group -mthumb \
-Wl,-Map="$(BUILD_DIR)/$(OUTPUT_FILE_NAME).map" --specs=nano.specs -Wl,--gc-sections -mcpu=cortex-m4 \
-T"app_libs/asf4/ld/same54p20a_flash.ld" \
-L"gcc/gcc"
	@echo "Finished linking: $@"

	@echo "Creating binary outputs..."
	"arm-none-eabi-objcopy" -O binary "$(OUTPUT_FILE_PATH)" "$(BUILD_DIR)/$(OUTPUT_FILE_NAME).bin"
	"arm-none-eabi-objcopy" -O ihex -R .eeprom -R .fuse -R .lock -R .signature \
        "$(OUTPUT_FILE_PATH)" "$(BUILD_DIR)/$(OUTPUT_FILE_NAME).hex"
	"arm-none-eabi-objcopy" -j .eeprom --set-section-flags=.eeprom=alloc,load --change-section-lma \
        .eeprom=0 --no-change-warnings -O binary "$(OUTPUT_FILE_PATH)" \
        "$(BUILD_DIR)/$(OUTPUT_FILE_NAME).eep" || exit 0
	"arm-none-eabi-objdump" -h -S "$(OUTPUT_FILE_PATH)" > "$(BUILD_DIR)/$(OUTPUT_FILE_NAME).lss"
	@echo "Generated: $(BUILD_DIR)/$(OUTPUT_FILE_NAME).bin, .hex, .eep, .lss"

# Compiler targets
$(BUILD_DIR)/%.o: %.c
	@echo "Compiling: $<"
	@mkdir -p $(dir $@)
	$(QUOTE)arm-none-eabi-gcc$(QUOTE) -x c -mthumb -DDEBUG -Os -ffunction-sections -mlong-calls -g3 -Wall -c -std=gnu99 \
-D__SAME54P20A__ -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16 \
$(DIR_INCLUDES) \
-MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -o "$@" "$<"

$(BUILD_DIR)/%.o: %.s
	@echo "Assembling: $<"
	@mkdir -p $(dir $@)
	$(QUOTE)arm-none-eabi-as$(QUOTE) -x c -mthumb -DDEBUG -Os -ffunction-sections -mlong-calls -g3 -Wall -c -std=gnu99 \
-D__SAME54P20A__ -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16 \
$(DIR_INCLUDES) \
-MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -o "$@" "$<"

$(BUILD_DIR)/%.o: %.S
	@echo "Assembling (preprocessing): $<"
	@mkdir -p $(dir $@)
	$(QUOTE)arm-none-eabi-gcc$(QUOTE) -x c -mthumb -DDEBUG -Os -ffunction-sections -mlong-calls -g3 -Wall -c -std=gnu99 \
-D__SAME54P20A__ -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16 \
$(DIR_INCLUDES) \
-MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -o "$@" "$<"

# Detect changes in the dependent files and recompile the respective object files
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),distclean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif
endif

# Create build directories
$(SUB_DIRS):
	@mkdir -p "$@"

# Clean target
clean:
	@echo "Cleaning build directory..."
	rm -rf $(BUILD_DIR)
	@echo "Clean completed."

# Distclean target  
distclean: clean
	@echo "Removing all generated files..."
	@echo "Distclean completed."