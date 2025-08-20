################################################################################
# SAME54 Embedded Project Makefile
# Transformed for better organization and maintainability
################################################################################

# Project Configuration
PROJECT = AtmelStart
MCU_NAME = same54p20a
BUILD_DIR = build

# Network Interface Selection
# Set NETWORK_INTERFACE to GMAC or KSZ8851SNL (default)
NETWORK_INTERFACE ?= KSZ8851SNL

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
DEFINES = -D__SAME54P20A__ -DCONF_SERCOM_4_SPI_ENABLE=1 -DCONF_SERCOM_4_SPI_MODE=0x03 -DCONF_SERCOM_4_SPI_BAUD=12000000 -DCONF_SERCOM_4_SPI_RXPO=3 -DCONF_SERCOM_4_SPI_TXPO=0

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
-I"$(BSP_DRIVERS_DIR)" \
-I"etc/port"

# Source Files organized by library
# FreeRTOS Files
FREERTOS_CFILES = \
$(FREERTOS_DIR)/queue.c \
$(FREERTOS_DIR)/list.c \
$(FREERTOS_DIR)/portable/MemMang/heap_4.c \
$(FREERTOS_DIR)/croutine.c \
$(FREERTOS_DIR)/event_groups.c \
$(FREERTOS_DIR)/timers.c \
$(FREERTOS_DIR)/stream_buffer.c \
$(FREERTOS_DIR)/tasks.c \
$(FREERTOS_DIR)/portable/GCC/ARM_CM4F/port.c

# LwIP Files
LWIP_CFILES = \

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
$(ASF4_DIR)/hal/src/hal_cache.c \
$(ASF4_DIR)/hal/src/hal_ext_irq.c \
$(ASF4_DIR)/hpl/eic/hpl_eic.c

# DOIP Configuration - Set to 1 for raw lwIP, 0 for socket implementation
DOIP_USE_RAW_LWIP ?= 1

# Common Driver Files
DRIVER_CFILES = \
$(DRIVERS_DIR)/driver_led.c \
$(DRIVERS_DIR)/driver_doip.c \
$(BSP_DRIVERS_DIR)/bsp_led.c

# Network Interface Selection
ifeq ($(NETWORK_INTERFACE), KSZ8851SNL)
DRIVER_CFILES += \
$(DRIVERS_DIR)/driver_spi.c \
$(DRIVERS_DIR)/driver_ksz8851snl.c \
$(DRIVERS_DIR)/driver_netif_doip.c \
$(BSP_DRIVERS_DIR)/bsp_spi.c \
$(BSP_DRIVERS_DIR)/bsp_ksz8851snl.c \
$(BSP_DRIVERS_DIR)/bsp_netif_doip.c \
$(BSP_DRIVERS_DIR)/bsp_netif_doip_ksz.c \
$(LWIP_DIR)/port/ethif_ksz8851snl.c
DEFINES += -DUSE_KSZ8851SNL_INTERFACE=1
ASF4_CFILES += $(ASF4_DIR)/hal/src/hal_spi_m_sync.c
DIR_INCLUDES += -I"$(LWIP_DIR)/port/include"
$(info Building with KSZ8851SNL SPI-Ethernet interface)
else
DRIVER_CFILES += \
$(DRIVERS_DIR)/driver_ethernet.c \
$(DRIVERS_DIR)/driver_net.c \
$(DRIVERS_DIR)/driver_net_lwip.c \
$(BSP_DRIVERS_DIR)/bsp_ethernet.c \
$(BSP_DRIVERS_DIR)/bsp_net.c
DEFINES += -DUSE_GMAC_INTERFACE=1
$(info Building with GMAC Ethernet interface)
endif

# DOIP BSP Implementation Selection
ifeq ($(DOIP_USE_RAW_LWIP), 1)
DEFINES += -DDOIP_USE_RAW_LWIP=1
else
DEFINES += -DDOIP_USE_RAW_LWIP=0
endif

# Application Files
APP_CFILES = \
main.c \
user_tasks.c \
rtt_printf.c \

# Simple Test Application Files (no LwIP)
SIMPLE_APP_CFILES = \
main_simple.c \
ksz_minimal_test.c \
rtt_printf.c

# Ethernet PHY Files (now integrated into PHY driver)
ETHERNET_PHY_CFILES =

# Third-party Library Files
SEGGER_RTT_CFILES = \
$(SEGGER_RTT_DIR)/RTT/SEGGER_RTT.c

PRINTF_CFILES = \
$(PRINTF_DIR)/printf.c

# Combine all source files
# Conditional file selection based on build type
ifeq ($(SIMPLE_BUILD), 1)
# Simple build - no LwIP, minimal dependencies
CFILES = \
$(FREERTOS_CFILES) \
$(ASF4_CFILES) \
$(BSP_DRIVERS_DIR)/bsp_ksz8851snl.c \
$(DRIVERS_DIR)/driver_ksz8851snl.c \
$(BSP_DRIVERS_DIR)/bsp_led.c \
$(DRIVERS_DIR)/driver_led.c \
$(BSP_DRIVERS_DIR)/bsp_spi.c \
$(DRIVERS_DIR)/driver_spi.c \
$(ASF4_DIR)/system_same54.c \
$(SIMPLE_APP_CFILES) \
$(SEGGER_RTT_CFILES) \
$(PRINTF_CFILES)
else
# Normal build - full functionality
CFILES = \
$(FREERTOS_CFILES) \
$(LWIP_CFILES) \
$(ASF4_CFILES) \
$(DRIVER_CFILES) \
$(APP_CFILES) \
$(ETHERNET_PHY_CFILES) \
$(SEGGER_RTT_CFILES) \
$(PRINTF_CFILES)
endif

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
.PHONY: all clean distclean rebuild size help init gmac ksz8851 clean-switch-interface switch-status simple

# Default target
all: init $(OUTPUT_FILE_PATH)
	@echo "Build completed successfully!"
	@echo "Output files in $(BUILD_DIR)/"

# Simple test target (no LwIP)
simple: clean
	@echo "Building simple KSZ8851SNL test (no LwIP)..."
	$(MAKE) SIMPLE_BUILD=1 NETWORK_INTERFACE=KSZ8851SNL all

# Help target
help:
	@echo "Available targets:"
	@echo "  all       - Build the project (default)"
	@echo "  simple    - Build simple KSZ8851SNL test (no LwIP)"
	@echo "  clean     - Remove build directory"
	@echo "  distclean - Remove all generated files"
	@echo "  rebuild   - Clean and build"
	@echo "  size      - Show memory usage"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Network Interface Selection:"
	@echo "  ksz8851   - Build with KSZ8851SNL SPI-Ethernet interface (default)"
	@echo "  gmac      - Build with GMAC Ethernet interface"
	@echo ""
	@echo "Simple Test Build:"
	@echo "  simple    - Minimal KSZ8851SNL test without LwIP dependencies"
	@echo "  clean-switch-interface - Clean for interface switch"
	@echo "  switch-status - Show current interface configuration"

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
	@$(MK_DIR) $(BUILD_DIR) 2>nul || true
	@$(QUOTE)$(C_COMPILER)$(QUOTE) $(C_OPTIONS) $(DEFINES) $(DIR_INCLUDES) \
	-MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -o "$@" "$<"

$(BUILD_DIR)/%.o: %.S
	$(info Assembling: $<)
	@$(MK_DIR) $(BUILD_DIR) 2>nul || true
	@$(QUOTE)$(C_COMPILER)$(QUOTE) $(ASM_OPTIONS) $(DEFINES) $(DIR_INCLUDES) \
	-MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -o "$@" "$<"

$(BUILD_DIR)/%.o: %.cpp
	$(info Compiling C++: $<)
	@$(MK_DIR) $(BUILD_DIR) 2>nul || true
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

# Network Interface Selection Targets
.PHONY: gmac ksz8851 clean-switch-interface switch-status

gmac:
	@echo "Building with GMAC Ethernet interface..."
	$(MAKE) clean
	$(MAKE) NETWORK_INTERFACE=GMAC all

ksz8851:
	@echo "Building with KSZ8851SNL SPI-Ethernet interface..."
	$(MAKE) clean
	$(MAKE) NETWORK_INTERFACE=KSZ8851SNL all

clean-switch-interface:
	@echo "Cleaning for network interface switch..."
	@$(RM) $(BUILD_DIR)
	@echo "Ready for network interface switch. Use 'make gmac' or 'make ksz8851'"

switch-status:
	@echo "Current network interface: $(NETWORK_INTERFACE)"
	@echo "Available options:"
	@echo "  make ksz8851  - Build with KSZ8851SNL SPI-Ethernet interface (default)"
	@echo "  make gmac     - Build with GMAC Ethernet interface"
	@echo "  make clean-switch-interface - Clean for interface switch"