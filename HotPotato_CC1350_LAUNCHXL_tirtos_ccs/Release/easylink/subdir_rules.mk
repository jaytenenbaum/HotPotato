################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
easylink/EasyLink.obj: ../easylink/EasyLink.c $(GEN_OPTS) | $(GEN_HDRS)
	@echo 'Building file: $<'
	@echo 'Invoking: ARM Compiler'
	"C:/ti/ccsv7/tools/compiler/ti-cgt-arm_16.9.4.LTS/bin/armcl" -mv7M3 --code_state=16 --float_support=vfplib -me -O2 --include_path="C:/Users/boot1_000/AdvanceComputerSystems3/rfEasyLinkRx_CC1350_LAUNCHXL_tirtos_ccs" --include_path="C:/ti/simplelink_cc13x0_sdk_1_50_00_08/kernel/tirtos/packages/ti/sysbios/posix" --include_path="C:/ti/ccsv7/tools/compiler/ti-cgt-arm_16.9.4.LTS/include" --define=DeviceFamily_CC13X0 -g --diag_warning=225 --diag_warning=255 --diag_wrap=off --display_error_number --gen_func_subsections=on --preproc_with_compile --preproc_dependency="easylink/EasyLink.d_raw" --obj_directory="easylink" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: $<'
	@echo ' '


