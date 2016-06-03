#
# Makefile
#
# This file is part of Touchbridge
#
# Copyright 2015 James L Macfarlane
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Put your stlink folder here so make burn will work.
STLINK=~/stlink.git

# Binaries will be generated with this name (.elf, .bin, .hex, etc)
TARGETS=tbg_hat.elf tbg_hco.elf tbg_input.elf tbg_hso.elf

# Put your STM32F4 library code directory here
#STM_COMMON=../STM32F4-Discovery_FW_V1.1.0
STM_COMMON=STM32F10x_StdPeriph_Lib_V3.5.0/

# Normally you shouldn't need to change anything below this line!
#######################################################################################

CC=arm-none-eabi-gcc
AS=arm-none-eabi-as
LD=arm-none-eabi-ld
OBJCOPY=arm-none-eabi-objcopy

CFLAGS  = -g -O2 -Wall -std=gnu99
CFLAGS += -mlittle-endian -mthumb -mcpu=cortex-m3 -mthumb-interwork
#CFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16

LDFLAGS += -Tstm32_flash.ld

CPPFLAGS += -I. -DSTM32F10X_MD

NODE_ADDR ?= "node_addr_undefined"
CPPFLAGS += -DTBG_NODE_ADDR=$(NODE_ADDR)

# Include files from STM libraries
CPPFLAGS += -I$(STM_COMMON)/Libraries/CMSIS/CM3/DeviceSupport/ST/STM32F10x
CPPFLAGS += -I$(STM_COMMON)/Libraries/CMSIS/CM3/CoreSupport
CPPFLAGS += -I$(STM_COMMON)/Libraries/STM32F10x_StdPeriph_Driver/inc

LOADLIBES=-lm

# add startup file to build
#SRCS += $(STM_COMMON)/Libraries/CMSIS/ST/STM32F10x/Source/Templates/TrueSTUDIO/startup_stm32f10x.s 

# Use ST's CAN library for now:
COMMON_DEPS +=  $(STM_COMMON)/Libraries/STM32F10x_StdPeriph_Driver/src/stm32f10x_can.o

# The CAN library needs these two:
COMMON_DEPS +=  $(STM_COMMON)/Libraries/STM32F10x_StdPeriph_Driver/src/stm32f10x_rcc.o
COMMON_DEPS +=  $(STM_COMMON)/Libraries/STM32F10x_StdPeriph_Driver/src/stm32f10x_gpio.o

COMMON_DEPS += startup_stm32f10x_md.o
COMMON_DEPS += system_stm32f10x.o
COMMON_DEPS += stm32f10x_it.o
COMMON_DEPS += newlib_stubs.o
COMMON_DEPS += core_cm3.o
COMMON_DEPS += misc.o

COMMON_DEPS += portpin.o
COMMON_DEPS += led.o
COMMON_DEPS += delay.o
COMMON_DEPS += uart.o
COMMON_DEPS += cdev.o
COMMON_DEPS += tbg_stm32.o
COMMON_DEPS += tbg_msg.o
COMMON_DEPS += version.o

all: $(TARGETS)

tbg_hat.elf: tbg_hat_main.o tbg_rpi.o $(COMMON_DEPS)

tbg_hco.elf: tbg_hco_main.o tbg_node.o $(COMMON_DEPS)

tbg_input.elf: tbg_input_main.o tbg_node.o $(COMMON_DEPS)

tbg_hso.elf: tbg_hso_main.o tbg_node.o $(COMMON_DEPS)


.PHONEY: version.c # Ensure this always gets re-made. There could be a better way?
version.c:
	(/bin/echo -n 'const char *version = "'; git log --pretty=format:'%ci #%h' -n 1; echo '";') > $@

%.elf:
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LOADLIBES) $(LDLIBS)

#$(PROJ_NAME).elf: $(SRCS)
#	$(CC) $(CFLAGS) $^ -o $@ $(LOADLIBES)

%.bin: %.elf 
	$(OBJCOPY) -O binary $< $@

#	$(OBJCOPY) -O ihex $(PROJ_NAME).elf $(PROJ_NAME).hex

clean:
	rm -f *.o *.elf *.hex *.bin version.c

# Flash the STM32F4
burn: proj
	$(STLINK)/st-flash write $(PROJ_NAME).bin 0x8000000

flash: proj
	openocd -f ./arm-usb-ocd_STM32F103.cfg  -c init -c targets -c "reset halt" -c "flash write_image erase $(PROJ_NAME).elf" -c "verify_image $(PROJ_NAME).elf" -c "reset run" -c shutdown

run:
	openocd -f ./arm-usb-ocd_STM32F103.cfg  -c init -c targets -c "reset run" -c shutdown

halt:
	openocd -f ./arm-usb-ocd_STM32F103.cfg  -c init -c targets -c "reset halt" -c shutdown

