##########################################################################################################################
# 蔗糖制作系统——终版（STM32F407ZG）
# 平台工作区被清空后重建的工程：外层框架用平台 STM32F407 基础模板，
# 应用层在 app/，内核层在 kernel/（接口对标 OpenHarmony LiteOS-M）。
##########################################################################################################################

TARGET = main
DEBUG = 1
OPT = -Og
BUILD_DIR = build

C_SOURCES =  \
app/main.c \
app/app_runtime.c \
kernel/los_kernel.c \
app/agri_control.c \
app/board_hal.c \
app/board_port.c \
app/board_sensors.c \
app/cane_hal.c \
app/cane_process.c \
app/cane_runtime.c \
app/cane_gui.c \
app/cane_wifi.c \
app/cane_cloud_json.c \
app/speech.c \
app/gui_font.c \
app/tank_sensor.c \
app/tft35.c \
STM32F4xx_FWLib/src/misc.c \
STM32F4xx_FWLib/src/stm32f4xx_rcc.c \
STM32F4xx_FWLib/src/stm32f4xx_gpio.c \
STM32F4xx_FWLib/src/stm32f4xx_usart.c \
STM32F4xx_FWLib/src/stm32f4xx_tim.c \
STM32F4xx_FWLib/src/stm32f4xx_syscfg.c \
STM32F4xx_FWLib/src/stm32f4xx_exti.c \
STM32F4xx_FWLib/src/stm32f4xx_spi.c \
USER/system_stm32f4xx.c

ASM_SOURCES = startup_stm32f407xx.s

PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
AS = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
else
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
endif
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

CPU = -mcpu=cortex-m4
FPU = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

AS_DEFS =
C_DEFS = -DUSE_STDPERIPH_DRIVER -DSTM32F407xx -DSTM32F40_41xxx
AS_INCLUDES =
C_INCLUDES = -Iapp -Ikernel/include -ICORE -ISTM32F4xx_FWLib/inc -IUSER
C_INCLUDES += -I__harmony_probe
C_INCLUDES += -I__harmony_probe
C_INCLUDES += -I__harmony_probe
C_INCLUDES += -I__harmony_probe/sub
C_INCLUDES += -I__harmony_probe

ASFLAGS = $(MCU) $(AS_DEFS) $(AS_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections
ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif
# 依赖文件（.d）不能在 CFLAGS 里用 $@ 拼：
#   这些规则是 $(eval) 生成的，规则文本会被二次展开，那时还没有自动变量，
#   -MF"$(@:%.o=%.d)" 会变成 -MF""，gcc 就把后面的源文件当成 -MF 的参数吃掉，
#   报 fatal error: no input files。依赖生成改到每条规则里显式写路径。

LDSCRIPT = STM32F407ZGTx_FLASH.ld
LIBS = -lc -lm -lnosys -u _printf_float -u _scanf_float
LIBDIR =
# 7 条 newlib 链接警告（_close/_read/_fstat/_isatty/_kill/_lseek/_getpid
# "not implemented and will always fail"）的真正解法是显式提供桩函数。
# 只加 -specs=nosys.specs 不管用：nano 库会先从 libc_nano.a 里解析
# 这几个符号，specs 根本没机会生效。
#
# 桩函数写在 app/main.c 里（不是单独的 syscalls.c）：
# main.c 一定参与链接，--gc-sections 不会把它丢掉（入口函数所在的目标文件
# 整体保留），libc 的 _xxx_r 重入层因此能直接调到我们的空实现。
# 只保留 -Wl,--gc-sections：text 约 87.9KB，比不开 gc 省 10KB。
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections
LDFLAGS += -Wl,--no-warn-rwx-segments

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

# 目标文件按「路径下划线化」命名：
#   app/main.c                 -> build/app_main.o
#   USER/system_stm32f4xx.c    -> build/USER_system_stm32f4xx.o
#   STM32F4xx_FWLib/src/misc.c -> build/STM32F4xx_FWLib_src_misc.o
#
# 以前用的是 $(notdir) + vpath，那是按「文件名」在目录列表里顺序搜。
# $(sort) 会把目录排成 STM32F4xx_FWLib/src/ 、USER/ 、app/ —— USER/ 在 app/ 前面，
# 于是 build/main.o 被平台残留的模板 USER/main.c 抢走（它 #include "sys.h"，而 sys.h 不在工程里），
# 报 fatal error: sys.h: No such file or directory。
# 现在每个源文件绑死自己那一条规则，同名文件互不干扰，残留文件也不会被误编。
OBJECTS = $(addprefix $(BUILD_DIR)/,$(subst /,_,$(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(subst /,_,$(ASM_SOURCES:.s=.o)))

# 注意：define 里的内容会被 $(eval) 再展开一次，所以不能写 $$@ / $$<（会变空），
# 这里直接拼 $(1) 得到的源文件和目标路径，不依赖自动变量。
define RULE_C
$(BUILD_DIR)/$(subst /,_,$(1:.c=.o)): $(1) Makefile | $(BUILD_DIR)
	@echo build $(BUILD_DIR)/$(subst /,_,$(1:.c=.o))
	@$(CC) -c $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/$(subst /,_,$(1:.c=.d)) $(1) -o $(BUILD_DIR)/$(subst /,_,$(1:.c=.o))
endef
$(foreach s,$(C_SOURCES),$(eval $(call RULE_C,$(s))))

define RULE_S
$(BUILD_DIR)/$(subst /,_,$(1:.s=.o)): $(1) Makefile | $(BUILD_DIR)
	@echo build $(BUILD_DIR)/$(subst /,_,$(1:.s=.o))
	@$(AS) -c $(CFLAGS) $(1) -o $(BUILD_DIR)/$(subst /,_,$(1:.s=.o))
endef
$(foreach s,$(ASM_SOURCES),$(eval $(call RULE_S,$(s))))

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	@echo build $@
	@$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir $@

clean:
	-rm -fR $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)

# *** EOF ***
