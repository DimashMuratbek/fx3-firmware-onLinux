################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := cs-rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: cypress-fx3.elf secondary-outputs

# Tool invocations
cypress-fx3.elf: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: Cross ARM C Linker'
	arm-none-eabi-gcc -mcpu=arm926ej-s -marm -mthumb-interwork -O3 -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -Wall  -g -T "/fw_build/fx3_fw/fx3.ld" -nostartfiles -Xlinker --gc-sections -L"/fw_lib/1_3_4/fx3_release" -L"/lib/gcc/arm-none-eabi/" -L"/arm-none-eabi/lib" -Wl,-Map,"cypress-fx3.map" -Wl,-d  -Wl,--no-wchar-size-warning -Wl,--entry,CyU3PFirmwareEntry -o "cypress-fx3.elf" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '
	$(MAKE) --no-print-directory post-build

# Other Targets
clean:
	-$(RM) $(OBJS)$(ASM_DEPS)$(S_UPPER_DEPS)$(C_DEPS) cypress-fx3.elf
	-@echo ' '

post-build:
	-@echo 'Generate boot-loadable binary image'
	-elf2img.exe -i cypress-fx3.elf -o cypress-fx3.fw -v
	-@echo ' '

secondary-outputs:

.PHONY: all clean dependents
.SECONDARY: post-build

-include ../makefile.targets
