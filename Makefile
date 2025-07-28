################################################################################
# SAME54 Embedded Project Makefile
# Transformed for better organization and maintainability
################################################################################

# Project Configuration
PROJECT = AtmelStart
MCU_NAME = same54p20a
BUILD_DIR = build

# Library Paths - Change these to point to different library versions
# 
# To use different library versions, simply modify these paths:
# Example: ASF4_DIR = ../shared_libs/asf4_v2.0
# Example: FREERTOS_DIR = ../freertos/FreeRTOS-Kernel-v10.4.0
# Example: LWIP_DIR = ../lwip/lwip-2.1.3
#
APP_LIBS_DIR = app_libs
ASF4_DIR = $(APP_LIBS_DIR)/asf4
FREERTOS_DIR = $(APP_LIBS_DIR)/FreeRTOS-Kernel
LWIP_DIR = $(APP_LIBS_DIR)/lwip
SEGGER_RTT_DIR = $(APP_LIBS_DIR)/SEGGER_RTT_V794b
PRINTF_DIR = $(APP_LIBS_DIR)/printf
ETHERNET_PHY_DIR = ethernet_phy
DRIVERS_DIR = drivers
BSP_DRIVERS_DIR = hw/same54/drivers

# Cross-platform toolchain detection
ifeq ($(OS),Windows_NT)
	SHELL = cmd.exe
	MK_DIR = mkdir
	RM = del /q
	C_COMPILER = arm-none-eabi-gcc.exe
	CPP_COMPILER = arm-none-eabi-g++.exe
	ASM_COMPILER = arm-none-eabi-as.exe
	OBJCOPY = arm-none-eabi-objcopy.exe
	OBJSIZE = arm-none-eabi-size.exe
	OBJDUMP = arm-none-eabi-objdump.exe
else
	# Unix-like systems (Linux, macOS, Cygwin, MinGW)
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
	RM = rm -rf
	C_COMPILER = arm-none-eabi-gcc
	CPP_COMPILER = arm-none-eabi-g++
	ASM_COMPILER = arm-none-eabi-as
	OBJCOPY = arm-none-eabi-objcopy
	OBJSIZE = arm-none-eabi-size
	OBJDUMP = arm-none-eabi-objdump
endif

# Compiler Options
CPU_OPTIONS = -mthumb -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16
COMMON_OPTIONS = -DDEBUG -Os -ffunction-sections -mlong-calls -g3 -Wall -c -std=gnu99
C_OPTIONS = $(COMMON_OPTIONS) $(CPU_OPTIONS) -x c
ASM_OPTIONS = $(COMMON_OPTIONS) $(CPU_OPTIONS) -x c

# MCU Definitions
DEFINES = -D__SAME54P20A__

# Linker Options
LINKER_SCRIPT = $(ASF4_DIR)/ld/same54p20a_flash.ld
LINKER_OPTIONS = $(CPU_OPTIONS) -Wl,--start-group -lm -Wl,--end-group --specs=nano.specs -Wl,--gc-sections -T$(LINKER_SCRIPT) -L"gcc/gcc"

# Include Directories
DIR_INCLUDES = \
-I"." \
-I"config" \
-I"examples" \
-I"$(ASF4_DIR)/hal/include" \
-I"$(ASF4_DIR)/hal/utils/include" \
-I"$(ASF4_DIR)/hpl/cmcc" \
-I"$(ASF4_DIR)/hpl/core" \
-I"$(ASF4_DIR)/hpl/dmac" \
-I"$(ASF4_DIR)/hpl/gclk" \
-I"$(ASF4_DIR)/hpl/mclk" \
-I"$(ASF4_DIR)/hpl/osc32kctrl" \
-I"$(ASF4_DIR)/hpl/oscctrl" \
-I"$(ASF4_DIR)/hpl/pm" \
-I"$(ASF4_DIR)/hpl/port" \
-I"$(ASF4_DIR)/hpl/ramecc" \
-I"$(ASF4_DIR)/hpl/sercom" \
-I"$(ASF4_DIR)/hri" \
-I"config" \
-I"thirdparty/RTOS" \
-I"$(FREERTOS_DIR)/include" \
-I"thirdparty/RTOS" \
-I"$(FREERTOS_DIR)/portable/GCC/ARM_CM4F" \
-I"$(FREERTOS_DIR)/portable/MemMang" \
-I"$(LWIP_DIR)/src/include" \
-I"$(LWIP_DIR)/contrib/ports/freertos/include" \
-I"$(LWIP_DIR)/port" \
-I"$(LWIP_DIR)/port/include" \
-I"$(ETHERNET_PHY_DIR)" \
-I"CMSIS/Core/Include" \
-I"include" \
-I"$(SEGGER_RTT_DIR)/RTT" \
-I"$(SEGGER_RTT_DIR)/Config" \
-I"$(PRINTF_DIR)" \
-I"$(DRIVERS_DIR)" \
-I"$(BSP_DRIVERS_DIR)"

# Source Files organized by library
# FreeRTOS Files
FREERTOS_CFILES = \
$(FREERTOS_DIR)/queue.c \
$(FREERTOS_DIR)/list.c \
$(FREERTOS_DIR)/portable/MemMang/heap_2.c \
$(FREERTOS_DIR)/croutine.c \
$(FREERTOS_DIR)/event_groups.c \
$(FREERTOS_DIR)/timers.c \
$(FREERTOS_DIR)/stream_buffer.c \
$(FREERTOS_DIR)/tasks.c \
$(FREERTOS_DIR)/portable/GCC/ARM_CM4F/port.c

# LwIP Files
LWIP_CFILES = \
$(LWIP_DIR)/src/core/ipv4/icmp.c \
$(LWIP_DIR)/src/core/def.c \
$(LWIP_DIR)/src/api/netbuf.c \
$(LWIP_DIR)/src/core/sys.c \
$(LWIP_DIR)/src/core/ipv4/autoip.c \
$(LWIP_DIR)/src/core/timeouts.c \
$(LWIP_DIR)/src/api/err.c \
$(LWIP_DIR)/src/api/api_msg.c \
$(LWIP_DIR)/src/core/tcp_out.c \
$(LWIP_DIR)/src/core/ipv4/ip4_frag.c \
$(LWIP_DIR)/src/core/pbuf.c \
$(LWIP_DIR)/src/core/tcp_in.c \
$(LWIP_DIR)/src/core/udp.c \
$(LWIP_DIR)/src/api/netdb.c \
$(LWIP_DIR)/src/core/memp.c \
$(LWIP_DIR)/src/core/ipv4/etharp.c \
$(LWIP_DIR)/src/core/ipv4/dhcp.c \
$(LWIP_DIR)/src/core/raw.c \
$(LWIP_DIR)/src/core/ipv4/ip4.c \
$(LWIP_DIR)/src/core/mem.c \
$(LWIP_DIR)/src/core/tcp.c \
$(LWIP_DIR)/src/netif/slipif.c \
$(LWIP_DIR)/contrib/ports/freertos/sys_arch.c \
$(LWIP_DIR)/src/core/init.c \
$(LWIP_DIR)/src/core/inet_chksum.c \
$(LWIP_DIR)/src/core/ip.c \
$(LWIP_DIR)/src/core/dns.c \
$(LWIP_DIR)/src/core/ipv4/igmp.c \
$(LWIP_DIR)/src/core/stats.c \
$(LWIP_DIR)/src/core/ipv4/ip4_addr.c \
$(LWIP_DIR)/src/core/ipv4/acd.c \
$(LWIP_DIR)/src/netif/ethernet.c \
$(LWIP_DIR)/src/api/netifapi.c \
$(LWIP_DIR)/src/api/sockets.c \
$(LWIP_DIR)/src/core/netif.c \
$(LWIP_DIR)/src/api/tcpip.c \
$(LWIP_DIR)/src/api/api_lib.c \
$(LWIP_DIR)/port/ethif_mac.c

# ASF4 Files
ASF4_CFILES = \
$(ASF4_DIR)/hal/utils/src/utils_syscalls.c \
$(ASF4_DIR)/hpl/pm/hpl_pm.c \
$(ASF4_DIR)/hal/src/hal_usart_sync.c \
$(ASF4_DIR)/hpl/gclk/hpl_gclk.c \
$(ASF4_DIR)/hal/src/hal_gpio.c \
$(ASF4_DIR)/hal/utils/src/utils_list.c \
$(ASF4_DIR)/hpl/dmac/hpl_dmac.c \
$(ASF4_DIR)/hpl/osc32kctrl/hpl_osc32kctrl.c \
$(ASF4_DIR)/hal/utils/src/utils_event.c \
$(ASF4_DIR)/hal/src/hal_mac_async.c \
$(ASF4_DIR)/hpl/cmcc/hpl_cmcc.c \
$(ASF4_DIR)/hal/src/hal_atomic.c \
$(ASF4_DIR)/hal/src/hal_io.c \
$(ASF4_DIR)/hpl/core/hpl_core_m4.c \
$(ASF4_DIR)/hal/src/hal_delay.c \
$(ASF4_DIR)/hpl/core/hpl_init.c \
$(ASF4_DIR)/hpl/oscctrl/hpl_oscctrl.c \
$(ASF4_DIR)/hal/src/hal_init.c \
$(ASF4_DIR)/hpl/sercom/hpl_sercom.c \
$(ASF4_DIR)/hal/src/hal_sleep.c \
$(ASF4_DIR)/hpl/gmac/hpl_gmac.c \
$(ASF4_DIR)/hpl/ramecc/hpl_ramecc.c \
$(ASF4_DIR)/hal/utils/src/utils_assert.c \
$(ASF4_DIR)/hpl/mclk/hpl_mclk.c \
$(ASF4_DIR)/hal/src/hal_cache.c

# Driver Files
DRIVER_CFILES = \
$(DRIVERS_DIR)/driver_led.c \
$(DRIVERS_DIR)/driver_ethernet.c \
$(DRIVERS_DIR)/driver_phy.c \
$(DRIVERS_DIR)/driver_net.c \
$(DRIVERS_DIR)/driver_net_lwip.c \
$(BSP_DRIVERS_DIR)/bsp_led.c \
$(BSP_DRIVERS_DIR)/bsp_ethernet.c \
$(BSP_DRIVERS_DIR)/bsp_phy.c \
$(BSP_DRIVERS_DIR)/bsp_net.c

# Application Files
APP_CFILES = \
main.c \
eth_ipstack_main.c \
webserver_tasks.c \
rtt_printf.c \
network_events.c \
doip_client.c

# Ethernet PHY Files (now integrated into PHY driver)
ETHERNET_PHY_CFILES =

# Third-party Library Files
SEGGER_RTT_CFILES = \
$(SEGGER_RTT_DIR)/RTT/SEGGER_RTT.c

PRINTF_CFILES = \
$(PRINTF_DIR)/printf.c

# Combine all source files
CFILES = \
$(FREERTOS_CFILES) \
$(LWIP_CFILES) \
$(ASF4_CFILES) \
$(DRIVER_CFILES) \
$(APP_CFILES) \
$(ETHERNET_PHY_CFILES) \
$(SEGGER_RTT_CFILES) \
$(PRINTF_CFILES)

# Assembly Files
ASMFILES = \
$(ASF4_DIR)/startup_same54.S

# C++ Files (if any)
CPPFILES =

# Setup VPATH for source file discovery
SOURCE_DIRS := $(sort $(dir $(CFILES)))
SOURCE_DIRS += $(sort $(dir $(ASMFILES)))
SOURCE_DIRS += $(sort $(dir $(CPPFILES)))
VPATH = $(SOURCE_DIRS)

# Generate object file lists
C_FILENAMES := $(notdir $(CFILES))
ASM_FILENAMES := $(notdir $(ASMFILES))
CPP_FILENAMES := $(notdir $(CPPFILES))

OBJ_FILES := $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_FILENAMES))
OBJ_FILES += $(patsubst %.S, $(BUILD_DIR)/%.o, $(ASM_FILENAMES))
OBJ_FILES += $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(CPP_FILENAMES))

# Dependency files
DEPS := $(OBJ_FILES:%.o=%.d)

# Output files
OUTPUT_FILE_PATH := $(BUILD_DIR)/$(PROJECT).elf
QUOTE := "

# Phony targets
.PHONY: all clean distclean rebuild size help init

# Default target
all: init $(OUTPUT_FILE_PATH)
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

# Size target with enhanced reporting
size: $(OUTPUT_FILE_PATH)
	@echo "Memory usage:"
	@$(OBJSIZE) $(OUTPUT_FILE_PATH)

# Rebuild target
rebuild: clean all

# Initialize build directories
init:
	@$(MK_DIR) $(BUILD_DIR) 2>/dev/null || true

# Linker target
$(OUTPUT_FILE_PATH): $(OBJ_FILES)
	@echo "Linking target: $@"
	@echo "Invoking: ARM/GNU Linker"
	$(QUOTE)$(C_COMPILER)$(QUOTE) -o $(OUTPUT_FILE_PATH) $(OBJ_FILES) $(LINKER_OPTIONS) \
	-Wl,-Map="$(BUILD_DIR)/$(PROJECT).map"
	@echo "Finished linking: $@"
	
	@echo "Creating binary outputs..."
	$(QUOTE)$(OBJCOPY)$(QUOTE) -O binary $(OUTPUT_FILE_PATH) $(BUILD_DIR)/$(PROJECT).bin
	$(QUOTE)$(OBJCOPY)$(QUOTE) -O ihex -R .eeprom -R .fuse -R .lock -R .signature \
		$(OUTPUT_FILE_PATH) $(BUILD_DIR)/$(PROJECT).hex
	$(QUOTE)$(OBJCOPY)$(QUOTE) -j .eeprom --set-section-flags=.eeprom=alloc,load --change-section-lma \
		.eeprom=0 --no-change-warnings -O binary $(OUTPUT_FILE_PATH) \
		$(BUILD_DIR)/$(PROJECT).eep || exit 0
	$(QUOTE)$(OBJCOPY)$(QUOTE) -O srec -R .eeprom -R .fuse -R .lock -R .signature \
		$(OUTPUT_FILE_PATH) $(BUILD_DIR)/$(PROJECT).srec
	$(QUOTE)$(OBJDUMP)$(QUOTE) -h -S $(OUTPUT_FILE_PATH) > $(BUILD_DIR)/$(PROJECT).lss
	@echo "Generated: $(BUILD_DIR)/$(PROJECT).bin, .hex, .eep, .srec, .lss"
	
	@echo "Memory usage:"
	@$(OBJSIZE) $(OUTPUT_FILE_PATH)

# Compilation rules with informative messages
$(BUILD_DIR)/%.o: %.c
	$(info Compiling: $<)
	@mkdir -p $(BUILD_DIR)
	@$(QUOTE)$(C_COMPILER)$(QUOTE) $(C_OPTIONS) $(DEFINES) $(DIR_INCLUDES) \
	-MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -o "$@" "$<"

$(BUILD_DIR)/%.o: %.S
	$(info Assembling: $<)
	@mkdir -p $(BUILD_DIR)
	@$(QUOTE)$(C_COMPILER)$(QUOTE) $(ASM_OPTIONS) $(DEFINES) $(DIR_INCLUDES) \
	-MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -o "$@" "$<"

$(BUILD_DIR)/%.o: %.cpp
	$(info Compiling C++: $<)
	@mkdir -p $(BUILD_DIR)
	@$(QUOTE)$(CPP_COMPILER)$(QUOTE) $(C_OPTIONS) $(DEFINES) $(DIR_INCLUDES) \
	-MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -o "$@" "$<"

# Include dependency files
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),distclean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif
endif

# Clean target
clean:
	@echo "Cleaning build directory..."
	$(RM) $(BUILD_DIR)
	@echo "Clean completed."

# Distclean target  
distclean: clean
	@echo "Removing all generated files..."
	@echo "Distclean completed."