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
$(BUILD_DIR)/hpl/ramecc \
$(BUILD_DIR)/hpl/gmac \
$(BUILD_DIR)/gcc \
$(BUILD_DIR)/gcc/gcc \
$(BUILD_DIR)/ethernet_phy \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/portable/GCC/ARM_CM4F \
$(BUILD_DIR)/examples \
$(BUILD_DIR)/hpl/oscctrl \
$(BUILD_DIR)/stdio_redirect \
$(BUILD_DIR)/hpl/osc32kctrl \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/api \
$(BUILD_DIR)/hpl/dmac \
$(BUILD_DIR)/hal/src \
$(BUILD_DIR)/stdio_redirect/gcc \
$(BUILD_DIR)/hal/utils/src \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3 \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/ipv4 \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif \
$(BUILD_DIR)/lwip/lwip-1.4.0/port \
$(BUILD_DIR)/hpl/pm \
$(BUILD_DIR)/hpl/cmcc \
$(BUILD_DIR)/hpl/gclk \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp \
$(BUILD_DIR)/hpl/mclk \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/snmp \
$(BUILD_DIR)/hpl/sercom \
$(BUILD_DIR)/hpl/core \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/portable/MemMang \
$(BUILD_DIR)/app_libs/SEGGER_RTT_V794b/RTT \
$(BUILD_DIR)/app_libs/printf

# List the object files
OBJS += \
$(BUILD_DIR)/stdio_redirect/stdio_io.o \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/queue.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/chpms.o \
$(BUILD_DIR)/hal/utils/src/utils_syscalls.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/ipv4/icmp.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/def.o \
$(BUILD_DIR)/hpl/pm/hpl_pm.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/magic.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/api/netbuf.o \
$(BUILD_DIR)/ethernet_phy_main.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/sys.o \
$(BUILD_DIR)/hal/src/hal_usart_sync.o \
$(BUILD_DIR)/stdio_redirect/gcc/read.o \
$(BUILD_DIR)/stdio_redirect/gcc/write.o \
$(BUILD_DIR)/hpl/gclk/hpl_gclk.o \
$(BUILD_DIR)/stdio_start.o \
$(BUILD_DIR)/ethernet_phy/ethernet_phy.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/randm.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/ipv4/inet.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/ipv4/autoip.o \
$(BUILD_DIR)/hal/src/hal_gpio.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/chap.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/md5.o \
$(BUILD_DIR)/gcc/gcc/startup_same54.o \
$(BUILD_DIR)/gcc/system_same54.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/timers.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/api/err.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/api/api_msg.o \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/portable/MemMang/heap_2.o \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/list.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/tcp_out.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/ipv4/ip_frag.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/pbuf.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/tcp_in.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/udp.o \
$(BUILD_DIR)/hal/utils/src/utils_list.o \
$(BUILD_DIR)/hpl/dmac/hpl_dmac.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/snmp/msg_out.o \
$(BUILD_DIR)/hpl/osc32kctrl/hpl_osc32kctrl.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/api/netdb.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/snmp/mib2.o \
$(BUILD_DIR)/driver_init.o \
$(BUILD_DIR)/main.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/snmp/msg_in.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/memp.o \
$(BUILD_DIR)/hal/utils/src/utils_event.o \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/rtos_port.o \
$(BUILD_DIR)/hal/src/hal_mac_async.o \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/croutine.o \
$(BUILD_DIR)/hpl/cmcc/hpl_cmcc.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/lcp.o \
$(BUILD_DIR)/atmel_start.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/etharp.o \
$(BUILD_DIR)/hal/src/hal_atomic.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/dhcp.o \
$(BUILD_DIR)/hal/src/hal_io.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/fsm.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/ppp_oe.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/raw.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/ipv4/ip.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/mem.o \
$(BUILD_DIR)/hpl/core/hpl_core_m4.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/tcp.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/port/ethif_mac.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/api/api_lib.o \
$(BUILD_DIR)/eth_ipstack_main.o \
$(BUILD_DIR)/hal/src/hal_delay.o \
$(BUILD_DIR)/hpl/core/hpl_init.o \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/event_groups.o \
$(BUILD_DIR)/hpl/oscctrl/hpl_oscctrl.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/snmp/asn1_enc.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/pap.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/slipif.o \
$(BUILD_DIR)/hal/src/hal_init.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/vj.o \
$(BUILD_DIR)/examples/driver_examples.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/port/sys_arch.o \
$(BUILD_DIR)/hpl/sercom/hpl_sercom.o \
$(BUILD_DIR)/webserver_tasks.o \
$(BUILD_DIR)/hal/src/hal_sleep.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/init.o \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/timers.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/ipv4/inet_chksum.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/dns.o \
$(BUILD_DIR)/lwip_socket_api.o \
$(BUILD_DIR)/hpl/gmac/hpl_gmac.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/snmp/asn1_dec.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/ipv4/igmp.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/ipcp.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/stats.o \
$(BUILD_DIR)/hpl/ramecc/hpl_ramecc.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/ppp.o \
$(BUILD_DIR)/hal/utils/src/utils_assert.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/ipv4/ip_addr.o \
$(BUILD_DIR)/hpl/mclk/hpl_mclk.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/snmp/mib_structs.o \
$(BUILD_DIR)/rtos_start.o \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/tasks.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/api/netifapi.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/netif/ppp/auth.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/api/sockets.o \
$(BUILD_DIR)/thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/portable/GCC/ARM_CM4F/port.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/core/netif.o \
$(BUILD_DIR)/hal/src/hal_cache.o \
$(BUILD_DIR)/lwip/lwip-1.4.0/src/api/tcpip.o \
$(BUILD_DIR)/app_libs/SEGGER_RTT_V794b/RTT/SEGGER_RTT.o \
$(BUILD_DIR)/app_libs/printf/printf.o \
$(BUILD_DIR)/rtt_printf.o

# List the directories containing header files
DIR_INCLUDES += \
-I"." \
-I"config" \
-I"examples" \
-I"hal/include" \
-I"hal/utils/include" \
-I"hpl/cmcc" \
-I"hpl/core" \
-I"hpl/dmac" \
-I"hpl/gclk" \
-I"hpl/mclk" \
-I"hpl/osc32kctrl" \
-I"hpl/oscctrl" \
-I"hpl/pm" \
-I"hpl/port" \
-I"hpl/ramecc" \
-I"hpl/sercom" \
-I"hri" \
-I"config" \
-I"thirdparty/RTOS" \
-I"thirdparty/RTOS/freertos/FreeRTOSV8.2.3" \
-I"thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/include" \
-I"thirdparty/RTOS/freertos/FreeRTOSV8.2.3/Source/portable/GCC/ARM_CM4F" \
-I"thirdparty/RTOS/freertos/FreeRTOSV8.2.3/module_config" \
-I"lwip/lwip-1.4.0/port" \
-I"lwip/lwip-1.4.0/port/include" \
-I"lwip/lwip-1.4.0/src/include" \
-I"lwip/lwip-1.4.0/src/include/ipv4" \
-I"lwip/lwip-1.4.0/src/include/lwip" \
-I"ethernet_phy" \
-I"stdio_redirect" \
-I"CMSIS/Core/Include" \
-I"include" \
-I"app_libs/SEGGER_RTT_V794b/RTT" \
-I"app_libs/SEGGER_RTT_V794b/Config" \
-I"app_libs/printf"

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
-T"gcc/gcc/same54p20a_flash.ld" \
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